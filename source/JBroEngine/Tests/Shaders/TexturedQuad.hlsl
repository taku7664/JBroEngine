// RHI 의 텍스처 바인딩을 확인하는 셰이더다. 엔진이 쓰는 것이 아니다.
//
// 화면 좌표를 그대로 받는다 — 카메라도 상수도 없다. 확인하려는 것은 하나뿐이고,
// 사이에 낀 것이 적을수록 틀렸을 때 어디가 틀렸는지 좁다.
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float2 uv       : ATTRIBUTE1;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv);
}
