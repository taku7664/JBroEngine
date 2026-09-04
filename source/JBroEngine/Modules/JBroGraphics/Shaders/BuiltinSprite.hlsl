cbuffer ViewConstants : register(b0)
{
    row_major float4x4 gViewProjection;
};

struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float4 worldRow0 : ATTRIBUTE1;
    float4 worldRow1 : ATTRIBUTE2;
    float4 worldRow2 : ATTRIBUTE3;
    float4 worldRow3 : ATTRIBUTE4;
    float4 tint : ATTRIBUTE5;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 tint : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
    const float4 localPosition = float4(input.position, 0.0f, 1.0f);
    const float4 worldPosition = float4(
        dot(input.worldRow0, localPosition),
        dot(input.worldRow1, localPosition),
        dot(input.worldRow2, localPosition),
        dot(input.worldRow3, localPosition));

    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    output.tint = input.tint;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return input.tint;
}
