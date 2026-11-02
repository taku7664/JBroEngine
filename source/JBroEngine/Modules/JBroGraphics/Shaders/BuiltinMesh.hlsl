// See BuiltinSprite.hlsl for why the push-constant block has two spellings.
#if defined(JBRO_SPIRV)
struct ViewConstantsBlock
{
    row_major float4x4 viewProjection;
};
[[vk::push_constant]] ConstantBuffer<ViewConstantsBlock> gPush;
#define gViewProjection gPush.viewProjection
#else
cbuffer ViewConstants : register(b0)
{
    row_major float4x4 gViewProjection;
};
#endif

// Instance layout mirrors GpuMeshInstance in Renderer.h: a row-major 4x4 world matrix in four
// float4 rows followed by the tint. Column-vector convention like the sprite shader.
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
};

// Mesh materials (D-348) read the same output: the prelude's JBroMeshInput lists these in this order, which is how Vulkan links the stages.
struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

VertexOutput VSMain(VertexInput input)
{
    const float4x4 world = float4x4(input.worldRow0, input.worldRow1, input.worldRow2, input.worldRow3);
    const float4 worldPosition = mul(world, float4(input.position, 1.0f));
    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    // Rotation only: the upper 3x3 with scale left in. Good enough for one directional light;
    // a proper inverse transpose comes with materials.
    output.normal = normalize(mul((float3x3)world, input.normal));
    output.tint = input.tint;
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

// The view's 3D lights (D-366), the renderer's GpuMeshLights. A mesh material reads the same block at b2 through JBroMeshLight.
cbuffer MeshLights : register(b1)
{
    // x: the light count. y: 1 when the view has lights at all (an ambient light counts); 0 means the fixed light below.
    float4 gLightCount;
    float4 gLightAmbient;
    // xyz: the direction towards the light (w 0) or its position (w 1).
    float4 gLightPosition[16];
    // rgb: colour x intensity, a: the range of a point light.
    float4 gLightColor[16];
};

float3 MeshLight(float3 normal, float3 worldPosition)
{
    const float3 n = normalize(normal);
    const int count = (int)gLightCount.x;
    if (gLightCount.y < 0.5f)
    {
        // One fixed directional light plus ambient, for a view without lights.
        const float3 lightDirection = normalize(float3(0.4f, 0.8f, 0.45f));
        return (0.25f + 0.75f * saturate(dot(n, lightDirection))).xxx;
    }
    float3 light = gLightAmbient.rgb;
    for (int at = 0; at < count; ++at)
    {
        float3 towards = gLightPosition[at].xyz;
        float attenuation = 1.0f;
        if (gLightPosition[at].w > 0.5f)
        {
            const float3 offset = gLightPosition[at].xyz - worldPosition;
            const float distance = length(offset);
            towards = offset / max(distance, 0.0001f);
            const float fade = saturate(1.0f - distance / max(gLightColor[at].a, 0.0001f));
            attenuation = fade * fade;
        }
        light += gLightColor[at].rgb * saturate(dot(n, towards)) * attenuation;
    }
    return light;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return float4(input.tint.rgb * MeshLight(input.normal, input.worldPosition), input.tint.a);
}
