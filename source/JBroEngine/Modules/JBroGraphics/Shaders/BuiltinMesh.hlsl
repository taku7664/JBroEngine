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

float4 PSMain(VertexOutput input) : SV_TARGET
{
    // One fixed directional light plus ambient. A mesh material gets the same through JBroMeshLight (D-348).
    const float3 lightDirection = normalize(float3(0.4f, 0.8f, 0.45f));
    const float lambert = saturate(dot(normalize(input.normal), lightDirection));
    const float lighting = 0.25f + 0.75f * lambert;
    return float4(input.tint.rgb * lighting, input.tint.a);
}
