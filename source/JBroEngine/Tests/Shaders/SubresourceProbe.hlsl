// Probes RHI subresources: drawing into one mip, reading one mip, reading depth. The engine does not use it.
//
// One full screen triangle from the vertex index, as in ConstantProbe.hlsl, with a depth and a uv.
//   b1  vertex stage. placement: xy scales, zw moves. depth.x: the depth written.
//   b2  pixel stage. The colour PSTint paints.
//   t0  the texture PSSample and PSDepth read. s0 is a point sampler.
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer Placement : register(b1)
{
    float4 gPlacement;
    float4 gDepth;
};

cbuffer Tint : register(b2)
{
    float4 gTint;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    // 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3). uv runs 0..1 over the part that lands on the screen square.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    const float2 position = corner * 2.0f - 1.0f;
    VertexOutput output;
    output.position = float4(position * gPlacement.xy + gPlacement.zw, gDepth.x, 1.0f);
    output.uv = float2(corner.x, 1.0f - corner.y);
    return output;
}

float4 PSTint(VertexOutput input) : SV_TARGET
{
    return gTint;
}

// With a one-mip view bound (SetTextureMip) level 0 is that mip.
float4 PSSample(VertexOutput input) : SV_TARGET
{
    return gTexture.SampleLevel(gSampler, input.uv, 0.0f);
}

// A depth texture reads as one float in red.
float4 PSDepth(VertexOutput input) : SV_TARGET
{
    const float depth = gTexture.Load(int3(input.position.xy, 0)).r;
    return float4(depth, depth, depth, 1.0f);
}
