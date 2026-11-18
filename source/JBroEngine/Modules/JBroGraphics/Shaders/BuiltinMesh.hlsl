// Standard mesh materials (material-plan stage 3, D-402): Standard is glTF metallic-roughness PBR, Unlit is base
// colour plus emissive. Both share the vertex stage. Standard is lit by the view's 3D lights (D-367); a view
// without lights gets one fixed directional light plus a flat ambient.
//
// Colour space: material colours, the instance tint, the light colours and sRGB-viewed textures arrive linear.
// Lighting is linear and the result is encoded to sRGB on the way out, because the frame targets are UNORM. On an
// HDR scene target the encode still runs, so a standard mesh stays within 0..1 there (an open item of the merge).
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

// One block per material (Renderer::RegisterMeshMaterial). Layout mirrors GpuMaterialConstants in Renderer.h.
cbuffer MaterialConstants : register(b1)
{
    float4 gBaseColor;
    // rgb emissive; w is the alpha cutoff, below zero when the material is opaque.
    float4 gEmissiveCutoff;
    // x metallic, y roughness, z 1 when the material has a normal map, w unused.
    float4 gParams;
};

// The view's 3D lights (D-367), the renderer's GpuMeshLights. A shader material (D-348) reads the same block at b2
// through JBroMeshLight.
cbuffer MeshLights : register(b2)
{
    // x: the light count. y: 1 when the view has lights at all (an ambient light counts); 0 means the fixed light below.
    float4 gLightCount;
    float4 gLightAmbient;
    // xyz: the direction towards the light (w 0) or its position (w 1).
    float4 gLightPosition[16];
    // rgb: colour x intensity, a: the range of a point light.
    float4 gLightColor[16];
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

// Shader materials (D-348) read the same output: the prelude's JBroMeshInput lists the first four in this order,
// which is how Vulkan links the stages. The tangent comes last so that prefix stays the same.
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
    output.worldPosition = worldPosition.xyz;
    output.tangent = float4(normalize(mul((float3x3)world, input.tangent.xyz)), input.tangent.w);
    return output;
}

static const float Pi = 3.14159265f;
// The fixed light of a view without lights (towards the light).
static const float3 FixedLightDirection = normalize(float3(0.4f, 0.8f, 0.45f));
static const float FixedLightIntensity = 0.75f;
static const float FixedAmbientIntensity = 0.25f;

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

// One light's contribution: GGX distribution, Schlick-Smith visibility (k from the remapped roughness) and Schlick
// Fresnel. `radiance` is the light's colour x intensity; a white Lambert surface facing the light reaches it.
float3 Shade(float3 normal, float3 view, float3 towards, float3 radiance, float3 base, float metallic, float roughness)
{
    const float3 halfway = normalize(towards + view);
    const float normalDotLight = saturate(dot(normal, towards));
    const float normalDotView = max(dot(normal, view), 0.0001f);
    const float normalDotHalf = saturate(dot(normal, halfway));
    const float viewDotHalf = saturate(dot(view, halfway));
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared / (Pi * denominator * denominator);
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    const float geometry = (normalDotView / (normalDotView * (1.0f - k) + k)) * (normalDotLight / (normalDotLight * (1.0f - k) + k));
    const float3 reflectance = lerp(float3(0.04f, 0.04f, 0.04f), base, metallic);
    const float3 fresnel = reflectance + (1.0f - reflectance) * pow(1.0f - viewDotHalf, 5.0f);
    const float3 specular = distribution * geometry * fresnel / max(4.0f * normalDotView * normalDotLight, 0.0001f);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * base;
    return (diffuse + Pi * specular) * radiance * normalDotLight;
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

    float3 lit;
    if (gLightCount.y < 0.5f)
    {
        lit = FixedAmbientIntensity * base.rgb * occlusion
            + Shade(normal, view, FixedLightDirection, FixedLightIntensity.xxx, base.rgb, metallic, roughness);
    }
    else
    {
        lit = gLightAmbient.rgb * base.rgb * occlusion;
        const int count = (int)gLightCount.x;
        for (int at = 0; at < count; ++at)
        {
            // A directional light's direction is normalised here: the halfway vector needs a unit vector.
            float3 towards = gLightPosition[at].xyz / max(length(gLightPosition[at].xyz), 0.0001f);
            float attenuation = 1.0f;
            if (gLightPosition[at].w > 0.5f)
            {
                const float3 offset = gLightPosition[at].xyz - input.worldPosition;
                const float distance = length(offset);
                towards = offset / max(distance, 0.0001f);
                const float fade = saturate(1.0f - distance / max(gLightColor[at].a, 0.0001f));
                attenuation = fade * fade;
            }
            lit += Shade(normal, view, towards, gLightColor[at].rgb * attenuation, base.rgb, metallic, roughness);
        }
    }
    return float4(LinearToSrgb(lit + Emissive(input)), base.a);
}

float4 PSUnlitMain(VertexOutput input) : SV_TARGET
{
    const float4 base = BaseColour(input);
    ClipCutout(base.a);
    return float4(LinearToSrgb(base.rgb + Emissive(input)), base.a);
}
