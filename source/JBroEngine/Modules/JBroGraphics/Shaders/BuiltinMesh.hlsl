// Mesh materials (material-plan stage 3, D-399): Standard is glTF metallic-roughness PBR lit by the view's light
// block (D-407, lighting3d-plan); Unlit is base colour plus emissive. Both share the vertex stage.
//
// Colour space: material colours, the instance tint and sRGB-viewed textures arrive linear. Lighting is linear
// and the result is encoded to sRGB on the way out, because the frame targets are UNORM (no HDR target or tone map
// yet - postprocess-plan stage 6 moves this encode into the tone map).
//
// See BuiltinSprite.hlsl for why the push-constant block has two spellings.
#if defined(JBRO_SPIRV)
struct ViewConstantsBlock
{
    row_major float4x4 viewProjection;
    // xyz camera position in world space; w adds 1 when this mesh carries real tangents (a normal map may be read)
    // and 2 when it does not receive shadows (D-445).
    float4 cameraAndFlags;
};
[[vk::push_constant]] ConstantBuffer<ViewConstantsBlock> gPush;
#define gViewProjection gPush.viewProjection
#define gCameraAndFlags gPush.cameraAndFlags
#else
cbuffer ViewConstants : register(b0)
{
    row_major float4x4 gViewProjection;
    float4 gCameraAndFlags;
};
#endif

// One block per standard material (Renderer::RegisterMeshMaterial). Layout mirrors GpuMaterialConstants in Renderer.h.
cbuffer MaterialConstants : register(b1)
{
    float4 gBaseColor;
    // rgb emissive; w is the alpha cutoff, below zero when the material is opaque.
    float4 gEmissiveCutoff;
    // x metallic, y roughness, z 1 when the material has a normal map, w unused.
    float4 gParams;
};

// The view's lights (D-407). Layout mirrors GpuLightConstants in Renderer.h. A shader material (D-348) reads the
// same block at b2 through the prelude's JBroLights. A view that submitted no 3D light gets
// the renderer's default block: the fixed directional light and ambient that came before light components.
struct Light
{
    // xyz position, w range (0 = unbounded).
    float4 positionRange;
    // xyz the direction the light travels, w kind (0 directional, 1 point, 2 spot).
    float4 directionKind;
    // rgb colour times intensity.
    float4 colour;
    // x scale, y offset of the spot cone on cos(angle) (glTF KHR_lights_punctual).
    float4 cone;
};
cbuffer LightConstants : register(b2)
{
    // rgb ambient, w light count.
    float4 gAmbientCount;
    Light gLights[64];
};

// Base colour and emissive come first: Unlit reads only those two, and a program's slots are a range from t0.
Texture2D gBaseColorTexture : register(t0);
Texture2D gEmissiveTexture : register(t1);
// glTF packing: G roughness, B metallic.
Texture2D gMetallicRoughnessTexture : register(t2);
Texture2D gNormalTexture : register(t3);
Texture2D gOcclusionTexture : register(t4);
SamplerState gSampler : register(s0);

// The view's shadows (D-445). Layout mirrors GpuShadowConstants in Renderer.h. The atlas is one 4096 x 4096 depth
// texture. Its top half holds 1024 tiles: the directional light's four cascades on the left (2 x 2), up to four spot
// lights on the right. Its bottom half holds 512 tiles, eight to a row: six cube faces for each of up to four point lights (D-446).
cbuffer ShadowConstants : register(b3)
{
    row_major float4x4 gCascades[4];
    row_major float4x4 gSpots[4];
    // Point light p's face f (+X, -X, +Y, -Y, +Z, -Z) is gPoints[p * 6 + f].
    row_major float4x4 gPoints[24];
    // The camera depth (distance along its forward) at which each cascade ends.
    float4 gCascadeSplit;
    // The world size of one texel in each cascade - how far a surface is pushed along its normal before the test.
    float4 gCascadeTexel;
    // Which of the view's lights each spot tile belongs to; below zero when the tile is unused.
    float4 gSpotLight;
    // The world size of one texel one unit away from each spot light.
    float4 gSpotTexel;
    // xyz the camera's forward; w the index of the directional light that casts, below zero when none.
    float4 gViewForward;
    // x the directional light's strength, y its depth bias, z the cascade count (0: no directional shadow), w the spot depth bias.
    float4 gShadowParams;
    float4 gSpotStrength;
    // Which of the view's lights each point slot belongs to (below zero when unused), the world size of one texel one
    // unit away from the light, and the shadow's strength (D-446).
    float4 gPointLight;
    float4 gPointTexel;
    float4 gPointStrength;
};
Texture2D<float> gShadowAtlas : register(t5);

#include "BuiltinEnvironment.hlsli"

// The canvas environment (D-447). Layout mirrors GpuEnvironmentConstants in Renderer.h. The default block adds no light:
// a black Color source and no sky map - the picture from before environments.
cbuffer EnvironmentConstants : register(b4)
{
    // rgb the Color source; w the source (0 Color, 1 Gradient, 2 Sky).
    float4 gEnvironmentAmbient;
    // The Gradient source: up, sideways and down.
    float4 gEnvironmentSkyColor;
    float4 gEnvironmentEquatorColor;
    float4 gEnvironmentGroundColor;
    // The Sky source: irradiance / pi of the sky map as nine SH coefficients (rgb), in the sky's own frame.
    float4 gEnvironmentSh[9];
    // x cos and y sin of the sky's rotation, z the sky's intensity, w the reflection intensity.
    float4 gEnvironmentParams;
    // x the sky map's mip count (0: no map); a rough surface reads a lower mip (D-447 prefiltered with GGX).
    float4 gEnvironmentMap;
};
Texture2D gEnvironmentTexture : register(t6);
SamplerState gEnvironmentSampler : register(s1);

// Unity's trilight: up blends towards the sky colour, down towards the ground, sideways is the equator.
float3 EnvironmentGradient(float3 direction)
{
    const float up = clamp(direction.y, -1.0f, 1.0f);
    return up >= 0.0f ? lerp(gEnvironmentEquatorColor.rgb, gEnvironmentSkyColor.rgb, up)
                      : lerp(gEnvironmentEquatorColor.rgb, gEnvironmentGroundColor.rgb, -up);
}

// Light reaching a surface facing `normal`, as a white Lambert surface would reflect it.
float3 EnvironmentDiffuse(float3 normal)
{
    if (gEnvironmentAmbient.w < 0.5f)
    {
        return gEnvironmentAmbient.rgb;
    }
    if (gEnvironmentAmbient.w < 1.5f)
    {
        return EnvironmentGradient(normal);
    }
    const float3 n = RotateIntoSky(normal, gEnvironmentParams.x, gEnvironmentParams.y);
    const float3 result = gEnvironmentSh[0].rgb * 0.282095f
        + gEnvironmentSh[1].rgb * (0.488603f * n.y) + gEnvironmentSh[2].rgb * (0.488603f * n.z) + gEnvironmentSh[3].rgb * (0.488603f * n.x)
        + gEnvironmentSh[4].rgb * (1.092548f * n.x * n.y) + gEnvironmentSh[5].rgb * (1.092548f * n.y * n.z)
        + gEnvironmentSh[6].rgb * (0.315392f * (3.0f * n.z * n.z - 1.0f)) + gEnvironmentSh[7].rgb * (1.092548f * n.x * n.z)
        + gEnvironmentSh[8].rgb * (0.546274f * (n.x * n.x - n.y * n.y));
    return max(result, float3(0.0f, 0.0f, 0.0f));
}

// Light arriving along `direction` for a surface of this roughness: the sky map's prefiltered mip, or the source's colour.
float3 EnvironmentSpecular(float3 direction, float roughness)
{
    if (gEnvironmentAmbient.w < 0.5f)
    {
        return gEnvironmentAmbient.rgb;
    }
    if (gEnvironmentAmbient.w < 1.5f || gEnvironmentMap.x < 0.5f)
    {
        return gEnvironmentAmbient.w < 1.5f ? EnvironmentGradient(direction) : float3(0.0f, 0.0f, 0.0f);
    }
    const float3 turned = RotateIntoSky(direction, gEnvironmentParams.x, gEnvironmentParams.y);
    const float level = roughness * (gEnvironmentMap.x - 1.0f);
    return gEnvironmentTexture.SampleLevel(gEnvironmentSampler, SkyMapUv(turned), level).rgb * gEnvironmentParams.z;
}

// Split-sum environment BRDF without a lookup table (Karis, "Physically Based Shading on Mobile", 2014).
float3 EnvironmentBrdf(float3 reflectance, float roughness, float normalDotView)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * normalDotView)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04f, 1.04f) * a004 + r.zw;
    return reflectance * scaleBias.x + scaleBias.y;
}

static const float ShadowTile = 1024.0f;
static const float PointShadowTile = 512.0f;

// 3 x 3 percentage-closer filter read with Load (no comparison sampler in the RHI). 1 is lit, 0 is fully shadowed.
// `origin` is the tile's top-left texel in the atlas and `size` its width.
float ShadowTest(float4x4 lightMatrix, float3 worldPosition, int2 origin, float size, float bias)
{
    const float4 clip = mul(lightMatrix, float4(worldPosition, 1.0f));
    if (clip.w <= 0.0f)
    {
        return 1.0f;
    }
    const float3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) >= 1.0f || abs(ndc.y) >= 1.0f || ndc.z <= 0.0f || ndc.z >= 1.0f)
    {
        return 1.0f;
    }
    const float2 texel = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * size - 0.5f;
    const int last = (int)size - 1;
    const int2 base = int2(floor(texel));
    float lit = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 at = origin + clamp(base + int2(x, y), int2(0, 0), int2(last, last));
            lit += (ndc.z - bias <= gShadowAtlas.Load(int3(at, 0))) ? 1.0f : 0.0f;
        }
    }
    return lit / 9.0f;
}

// How much of light `index` reaches this point (D-445): 1 unless that light casts a shadow here.
float ShadowFor(uint index, float3 worldPosition, float3 normal, float3 lightPosition)
{
    if (gCameraAndFlags.w >= 1.5f)
    {
        return 1.0f;
    }
    const float light = (float)index;
    if (light == gViewForward.w && gShadowParams.z > 0.5f)
    {
        const float depth = dot(worldPosition - gCameraAndFlags.xyz, gViewForward.xyz);
        const uint count = (uint)gShadowParams.z;
        [loop]
        for (uint cascade = 0; cascade < count; ++cascade)
        {
            if (depth <= gCascadeSplit[cascade])
            {
                const float3 pushed = worldPosition + normal * (gCascadeTexel[cascade] * 1.5f);
                const int2 origin = int2(cascade % 2, cascade / 2) * (int)ShadowTile;
                return lerp(1.0f, ShadowTest(gCascades[cascade], pushed, origin, ShadowTile, gShadowParams.y), gShadowParams.x);
            }
        }
        return 1.0f;
    }
    [unroll]
    for (uint spot = 0; spot < 4; ++spot)
    {
        if (gSpotLight[spot] >= 0.0f && gSpotLight[spot] == light)
        {
            const float distance = length(worldPosition - lightPosition);
            const float3 pushed = worldPosition + normal * (gSpotTexel[spot] * distance * 1.5f);
            const int2 origin = int2(2 + spot % 2, spot / 2) * (int)ShadowTile;
            return lerp(1.0f, ShadowTest(gSpots[spot], pushed, origin, ShadowTile, gShadowParams.w), gSpotStrength[spot]);
        }
    }
    [unroll]
    for (uint pointSlot = 0; pointSlot < 4; ++pointSlot)
    {
        if (gPointLight[pointSlot] >= 0.0f && gPointLight[pointSlot] == light)
        {
            const float distance = length(worldPosition - lightPosition);
            const float3 pushed = worldPosition + normal * (gPointTexel[pointSlot] * distance * 1.5f);
            // The face is the largest axis of the direction from the light - the same order the renderer drew them in.
            const float3 away = pushed - lightPosition;
            const float3 size = abs(away);
            uint face = 4 + (away.z < 0.0f ? 1 : 0);
            if (size.x >= size.y && size.x >= size.z)
            {
                face = away.x < 0.0f ? 1 : 0;
            }
            else if (size.y >= size.z)
            {
                face = 2 + (away.y < 0.0f ? 1 : 0);
            }
            const uint tile = pointSlot * 6 + face;
            const int2 origin = int2((tile % 8) * (int)PointShadowTile, (int)(2.0f * ShadowTile) + (tile / 8) * (int)PointShadowTile);
            return lerp(1.0f, ShadowTest(gPoints[tile], pushed, origin, PointShadowTile, gShadowParams.w), gPointStrength[pointSlot]);
        }
    }
    return 1.0f;
}

// Instance layout mirrors GpuMeshInstance in Renderer.h: a row-major 4x4 world matrix in four float4 rows followed
// by the tint. The UV and tangent come last (ATTRIBUTE7/8, MeshVertex offsets 24/32) so the instance attributes
// keep their numbers. Order matters: SPIR-V assigns input locations in declaration order, and the renderer binds
// location = attribute index.
struct VertexInput
{
    float3 position : ATTRIBUTE0;
    float3 normal : ATTRIBUTE1;
    float4 worldRow0 : ATTRIBUTE2;
    float4 worldRow1 : ATTRIBUTE3;
    float4 worldRow2 : ATTRIBUTE4;
    float4 worldRow3 : ATTRIBUTE5;
    float4 tint : ATTRIBUTE6;
    float2 uv : ATTRIBUTE7;
    float4 tangent : ATTRIBUTE8;
};

// Shader materials (D-348) read the same output: the prelude's JBroMeshInput lists the first four in this order,
// which is how Vulkan links the stages. The tangent comes last so that prefix stays the same (D-406).
struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 tangent : TANGENT0;
};

VertexOutput VSMain(VertexInput input)
{
    const float4x4 world = float4x4(input.worldRow0, input.worldRow1, input.worldRow2, input.worldRow3);
    const float4 worldPosition = mul(world, float4(input.position, 1.0f));
    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    // Rotation only: the upper 3x3 with scale left in. Exact for uniform scale; a proper inverse transpose comes
    // with non-uniform scaling support.
    output.normal = normalize(mul((float3x3)world, input.normal));
    output.tint = input.tint;
    output.uv = input.uv;
    output.tangent = float4(normalize(mul((float3x3)world, input.tangent.xyz)), input.tangent.w);
    output.worldPosition = worldPosition.xyz;
    return output;
}

// Skinned meshes (D-458, skeletal-plan stage 1b). A second vertex stream carries four bone numbers (as floats - the RHI
// has no integer vertex formats, and small integers are exact in a float) and four weights that sum to one
// (ATTRIBUTE9/10, after the instance and UV/tangent attributes so their numbers stay). The bones are skinning matrices
// (bone model matrix x inverse bind), three rows each, in model space; the instance world matrix then places the model.
// Each program reads them at the first slot after the blocks its pixel stage declares, so no program declares a slot
// it never reads: b5 for the Standard material (after the material, light, shadow and environment blocks), b2 for
// Unlit (after its material block), b1 for the shadow pass (it binds nothing else). Each entry point references only
// its own block, so the others are compiled out. Renderer::SubmitSkinPalette writes MaxSkinBones bones' worth per
// palette and binds it whole.
static const uint MaxSkinBones = 256;

cbuffer SkinConstants : register(b5)
{
    float4 gSkinRows[768];
};

cbuffer UnlitSkinConstants : register(b2)
{
    float4 gUnlitSkinRows[768];
};

cbuffer ShadowSkinConstants : register(b1)
{
    float4 gShadowSkinRows[768];
};

struct SkinnedVertexInput
{
    float3 position : ATTRIBUTE0;
    float3 normal : ATTRIBUTE1;
    float4 worldRow0 : ATTRIBUTE2;
    float4 worldRow1 : ATTRIBUTE3;
    float4 worldRow2 : ATTRIBUTE4;
    float4 worldRow3 : ATTRIBUTE5;
    float4 tint : ATTRIBUTE6;
    float2 uv : ATTRIBUTE7;
    float4 tangent : ATTRIBUTE8;
    float4 bones : ATTRIBUTE9;
    float4 weights : ATTRIBUTE10;
};

// The weighted sum of the vertex's four skinning matrices, as three rows - once per block, since an entry point must
// name the block it reads.
#define JBRO_SKIN_ROWS(NAME, ROWS)                                                              \
    void NAME(float4 bones, float4 weights, out float4 row0, out float4 row1, out float4 row2)  \
    {                                                                                           \
        row0 = float4(0.0f, 0.0f, 0.0f, 0.0f);                                                  \
        row1 = float4(0.0f, 0.0f, 0.0f, 0.0f);                                                  \
        row2 = float4(0.0f, 0.0f, 0.0f, 0.0f);                                                  \
        [unroll] for (uint slot = 0; slot < 4; ++slot)                                          \
        {                                                                                       \
            const uint bone = min((uint)(bones[slot] + 0.5f), MaxSkinBones - 1) * 3;           \
            row0 += ROWS[bone + 0] * weights[slot];                                             \
            row1 += ROWS[bone + 1] * weights[slot];                                             \
            row2 += ROWS[bone + 2] * weights[slot];                                             \
        }                                                                                       \
    }

JBRO_SKIN_ROWS(SkinRows, gSkinRows)
JBRO_SKIN_ROWS(UnlitSkinRows, gUnlitSkinRows)
JBRO_SKIN_ROWS(ShadowSkinRows, gShadowSkinRows)

// Moves the vertex by the summed rows and hands it to the ordinary vertex stage. Normals and tangents use the upper
// 3x3 (exact for rotations and uniform scale, like the instance matrix).
VertexInput Skin(SkinnedVertexInput input, float4 row0, float4 row1, float4 row2)
{
    VertexInput skinned;
    const float4 position = float4(input.position, 1.0f);
    skinned.position = float3(dot(row0, position), dot(row1, position), dot(row2, position));
    skinned.normal = float3(dot(row0.xyz, input.normal), dot(row1.xyz, input.normal), dot(row2.xyz, input.normal));
    skinned.worldRow0 = input.worldRow0;
    skinned.worldRow1 = input.worldRow1;
    skinned.worldRow2 = input.worldRow2;
    skinned.worldRow3 = input.worldRow3;
    skinned.tint = input.tint;
    skinned.uv = input.uv;
    skinned.tangent = float4(dot(row0.xyz, input.tangent.xyz), dot(row1.xyz, input.tangent.xyz), dot(row2.xyz, input.tangent.xyz), input.tangent.w);
    return skinned;
}

VertexOutput VSSkinnedMain(SkinnedVertexInput input)
{
    float4 row0;
    float4 row1;
    float4 row2;
    SkinRows(input.bones, input.weights, row0, row1, row2);
    return VSMain(Skin(input, row0, row1, row2));
}

VertexOutput VSUnlitSkinnedMain(SkinnedVertexInput input)
{
    float4 row0;
    float4 row1;
    float4 row2;
    UnlitSkinRows(input.bones, input.weights, row0, row1, row2);
    return VSMain(Skin(input, row0, row1, row2));
}

VertexOutput VSShadowSkinnedMain(SkinnedVertexInput input)
{
    float4 row0;
    float4 row1;
    float4 row2;
    ShadowSkinRows(input.bones, input.weights, row0, row1, row2);
    return VSMain(Skin(input, row0, row1, row2));
}

static const float Pi = 3.14159265f;

float3 LinearToSrgb(float3 colour)
{
    const float3 clamped = saturate(colour);
    const float3 low = clamped * 12.92f;
    const float3 high = 1.055f * pow(clamped, 1.0f / 2.4f) - 0.055f;
    // step(c, edge) is 1 where c <= edge. `select` would read better but fxc (SM 5.0) does not have it.
    return lerp(high, low, step(clamped, float3(0.0031308f, 0.0031308f, 0.0031308f)));
}

float4 BaseColour(VertexOutput input)
{
    return gBaseColor * gBaseColorTexture.Sample(gSampler, input.uv) * input.tint;
}

void ClipCutout(float alpha)
{
    if (gEmissiveCutoff.w >= 0.0f && alpha < gEmissiveCutoff.w)
    {
        discard;
    }
}

float3 Emissive(VertexOutput input)
{
    return gEmissiveCutoff.rgb * gEmissiveTexture.Sample(gSampler, input.uv).rgb;
}

float3 SurfaceNormal(VertexOutput input, bool frontFace)
{
    // A double-sided material (D-443) draws back faces too; seen from behind, the surface faces the other way.
    // Single-sided materials cull back faces, so this only ever flips for double-sided ones.
    const float3 normal = frontFace ? normalize(input.normal) : -normalize(input.normal);
    if (fmod(gCameraAndFlags.w, 2.0f) * gParams.z < 0.5f)
    {
        return normal;
    }
    // Tangent space from the vertex tangent; w picks the bitangent's side.
    const float3 tangent = normalize(input.tangent.xyz - normal * dot(normal, input.tangent.xyz));
    const float3 bitangent = cross(normal, tangent) * input.tangent.w;
    const float3 sampled = gNormalTexture.Sample(gSampler, input.uv).xyz * 2.0f - 1.0f;
    return normalize(sampled.x * tangent + sampled.y * bitangent + sampled.z * normal);
}

float4 PSMain(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    const float4 base = BaseColour(input);
    ClipCutout(base.a);
    const float4 packed = gMetallicRoughnessTexture.Sample(gSampler, input.uv);
    const float metallic = saturate(gParams.x * packed.b);
    const float roughness = clamp(gParams.y * packed.g, 0.045f, 1.0f);
    const float occlusion = gOcclusionTexture.Sample(gSampler, input.uv).r;

    const float3 normal = SurfaceNormal(input, frontFace);
    // The surface's own normal pushes the point off it before the shadow test; a normal map would bend the push.
    const float3 geometric = frontFace ? normalize(input.normal) : -normalize(input.normal);
    const float3 view = normalize(gCameraAndFlags.xyz - input.worldPosition);
    const float normalDotView = max(dot(normal, view), 0.0001f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    const float3 reflectance = lerp(float3(0.04f, 0.04f, 0.04f), base.rgb, metallic);

    float3 direct = float3(0.0f, 0.0f, 0.0f);
    const uint count = (uint)gAmbientCount.w;
    [loop]
    for (uint index = 0; index < count; ++index)
    {
        const Light light = gLights[index];
        float3 towards = -light.directionKind.xyz;
        float attenuation = 1.0f;
        if (light.directionKind.w > 0.5f)
        {
            // Point and spot: inverse square with glTF's smooth window to zero at the range. The distance never
            // drops under 0.01 so a surface touching the light does not blow up.
            const float3 toLight = light.positionRange.xyz - input.worldPosition;
            const float distanceSquared = max(dot(toLight, toLight), 0.0001f);
            towards = toLight * rsqrt(distanceSquared);
            attenuation = 1.0f / distanceSquared;
            if (light.positionRange.w > 0.0f)
            {
                const float ratio = distanceSquared / (light.positionRange.w * light.positionRange.w);
                attenuation *= saturate(1.0f - ratio * ratio);
            }
            if (light.directionKind.w > 1.5f)
            {
                const float cone = saturate(dot(light.directionKind.xyz, -towards) * light.cone.x + light.cone.y);
                attenuation *= cone * cone;
            }
        }
        attenuation *= ShadowFor(index, input.worldPosition, geometric, light.positionRange.xyz);
        const float normalDotLight = saturate(dot(normal, towards));
        const float3 halfway = normalize(towards + view);
        const float normalDotHalf = saturate(dot(normal, halfway));
        const float viewDotHalf = saturate(dot(view, halfway));

        // GGX distribution, Schlick-Smith visibility (k from the remapped roughness) and Schlick Fresnel.
        const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
        const float distribution = alphaSquared / (Pi * denominator * denominator);
        const float geometry = (normalDotView / (normalDotView * (1.0f - k) + k)) * (normalDotLight / (normalDotLight * (1.0f - k) + k));
        const float3 fresnel = reflectance + (1.0f - reflectance) * pow(1.0f - viewDotHalf, 5.0f);
        const float3 specular = distribution * geometry * fresnel / max(4.0f * normalDotView * normalDotLight, 0.0001f);
        const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * base.rgb;
        // The light's radiance is colour * Pi, so a white Lambert surface facing it reaches the colour.
        direct += (diffuse + Pi * specular) * light.colour.rgb * (attenuation * normalDotLight);
    }
    const float3 ambient = gAmbientCount.rgb * base.rgb * occlusion;
    // The canvas environment (D-447): diffuse light from every side, and the reflection of what the view's mirror direction sees.
    // The `Ambient` light above keeps its old term (no metallic) so scenes lit by it look as before.
    const float3 environmentDiffuse = EnvironmentDiffuse(normal) * base.rgb * (1.0f - metallic) * occlusion;
    const float3 environmentSpecular = EnvironmentSpecular(reflect(-view, normal), roughness)
        * EnvironmentBrdf(reflectance, roughness, normalDotView) * (occlusion * gEnvironmentParams.w);
    return float4(LinearToSrgb(ambient + environmentDiffuse + environmentSpecular + direct + Emissive(input)), base.a);
}

// The shadow map (D-445): depth only, written by the rasteriser.
void PSShadowMain()
{
}

float4 PSUnlitMain(VertexOutput input) : SV_TARGET
{
    const float4 base = BaseColour(input);
    ClipCutout(base.a);
    return float4(LinearToSrgb(base.rgb + Emissive(input)), base.a);
}
