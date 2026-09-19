// b0 is the RHI's push-constant block. dxc -spirv wants a [[vk::push_constant]] ConstantBuffer<T>,
// fxc (SM 5.0) knows neither the attribute nor ConstantBuffer<T>, so the two spellings live behind
// JBRO_SPIRV. The field name gViewProjection is the same on both sides.
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

// t0/s0: the sprite's texture and sampler (D-113). Sprites without a texture bind a 1x1 white
// texture, so the tint comes through unchanged. The Vulkan build shifts t# to binding 8+ and s# to
// 16+ (Compile.ps1), which is what the RHI's Vulkan backend expects.
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

// Instance layout mirrors GpuSpriteInstance in Renderer.h. Column-vector convention:
//   x' = linear.x * x + linear.y * y + translation.x
//   y' = linear.z * x + linear.w * y + translation.y
// The dropped 4x4 rows survive as translation.z (depth) and an implicit w of 1.
// uvRect is (uMin, vMin, uScale, vScale): a sheet cell is a sub-rectangle of the texture.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float4 worldLinear : ATTRIBUTE1;
    float3 worldTranslation : ATTRIBUTE2;
    float4 tint : ATTRIBUTE3;
    float4 uvRect : ATTRIBUTE4;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const float4 worldPosition = float4(
        dot(input.worldLinear.xy, input.position) + input.worldTranslation.x,
        dot(input.worldLinear.zw, input.position) + input.worldTranslation.y,
        input.worldTranslation.z,
        1.0f);

    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    output.tint = input.tint;
    // The unit quad runs -0.5..0.5 with +y up; texture rows run top to bottom, so v flips.
    const float2 unit = float2(input.position.x + 0.5f, 0.5f - input.position.y);
    output.uv = unit * input.uvRect.zw + input.uvRect.xy;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv) * input.tint;
}
