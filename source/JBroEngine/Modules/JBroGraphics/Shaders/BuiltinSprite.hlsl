cbuffer ViewConstants : register(b0)
{
    row_major float4x4 gViewProjection;
};

// Instance layout mirrors GpuSpriteInstance in Renderer.h. Column-vector convention:
//   x' = linear.x * x + linear.y * y + translation.x
//   y' = linear.z * x + linear.w * y + translation.y
// The dropped 4x4 rows survive as translation.z (depth) and an implicit w of 1.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float4 worldLinear : ATTRIBUTE1;
    float3 worldTranslation : ATTRIBUTE2;
    float4 tint : ATTRIBUTE3;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 tint : COLOR0;
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
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return input.tint;
}
