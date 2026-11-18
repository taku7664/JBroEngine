// Mesh materials (material-plan stage 3, D-346): Standard is glTF metallic-roughness PBR lit by the view's light
// block (D-353, lighting3d-plan); Unlit is base colour plus emissive. Both share the vertex stage.
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
    // xyz camera position in world space; w is 1 when this mesh carries real tangents (a normal map may be read).
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

// One block per material (Renderer::RegisterMaterial). Layout mirrors GpuMaterialConstants in Renderer.h.
cbuffer MaterialConstants : register(b1)
{
    float4 gBaseColor;
    // rgb emissive; w is the alpha cutoff, below zero when the material is opaque.
    float4 gEmissiveCutoff;
    // x metallic, y roughness, z 1 when the material has a normal map, w unused.
    float4 gParams;
};

// The view's lights (D-353). Layout mirrors GpuLightConstants in Renderer.h. A view that submitted no 3D light gets
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

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
    float3 worldPosition : TEXCOORD1;
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

float3 SurfaceNormal(VertexOutput input)
{
    const float3 normal = normalize(input.normal);
    if (gCameraAndFlags.w * gParams.z < 0.5f)
    {
        return normal;
    }
    // Tangent space from the vertex tangent; w picks the bitangent's side.
    const float3 tangent = normalize(input.tangent.xyz - normal * dot(normal, input.tangent.xyz));
    const float3 bitangent = cross(normal, tangent) * input.tangent.w;
    const float3 sampled = gNormalTexture.Sample(gSampler, input.uv).xyz * 2.0f - 1.0f;
    return normalize(sampled.x * tangent + sampled.y * bitangent + sampled.z * normal);
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    const float4 base = BaseColour(input);
    ClipCutout(base.a);
    const float4 packed = gMetallicRoughnessTexture.Sample(gSampler, input.uv);
    const float metallic = saturate(gParams.x * packed.b);
    const float roughness = clamp(gParams.y * packed.g, 0.045f, 1.0f);
    const float occlusion = gOcclusionTexture.Sample(gSampler, input.uv).r;

    const float3 normal = SurfaceNormal(input);
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
    return float4(LinearToSrgb(ambient + direct + Emissive(input)), base.a);
}

float4 PSUnlitMain(VertexOutput input) : SV_TARGET
{
    const float4 base = BaseColour(input);
    ClipCutout(base.a);
    return float4(LinearToSrgb(base.rgb + Emissive(input)), base.a);
}
