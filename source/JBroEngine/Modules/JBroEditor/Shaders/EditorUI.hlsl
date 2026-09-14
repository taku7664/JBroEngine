// ImGui 의 드로우 리스트를 그리는 셰이더다.
//
// 정점 레이아웃은 `ImDrawVert` 를 그대로 따른다 — 위치 2, UV 2, 색 4바이트.
// 색을 float4 로 부풀리지 않는 이유는 정점 수가 UI 에서 쉽게 수만 개가 되기 때문이다.
//
// 투영은 푸시 상수로 받는다. UI 는 화면 좌표로 그려지고 그 행렬은 프레임마다
// 창 크기에서 나오므로, 상수 버퍼를 따로 둘 값이 아니다.
cbuffer PushConstants : register(b0)
{
    float2 gScale;
    float2 gTranslate;
};

Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float2 uv       : ATTRIBUTE1;
    float4 color    : ATTRIBUTE2;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * gScale + gTranslate, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return input.color * gTexture.Sample(gSampler, input.uv);
}
