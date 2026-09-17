// Draws ImGui draw lists.
//
// The vertex layout follows ImDrawVert as is: position 2, uv 2, colour 4 bytes.
// The colour is not widened to float4 because UI vertex counts easily reach tens of thousands.
//
// The projection comes in as push constants. UI is drawn in screen space and that matrix
// falls out of the window size every frame, so it is not worth a constant buffer of its own.
//
// Comments in shader files stay ASCII: this file feeds dxc (DXIL), fxc (DXBC for D3D11) and
// dxc -spirv, and fxc rejects both a UTF-8 BOM and bytes it cannot map to the ANSI code page.
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
