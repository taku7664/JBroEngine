// Probes the RHI constant buffers and the non-indexed draw. The engine does not use it.
//
// No vertex buffer: the vertex index alone makes one triangle that covers the screen - the shape every
// full-screen post process pass draws. Two constant buffers are read; b0 is left alone because it is the
// push constant register (BindingLayoutDesc in RHI.h).
//   b1  read by the vertex stage. xy scales, zw moves.
//   b2  read by the pixel stage. The colour to paint.
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer Placement : register(b1)
{
    float4 gPlacement;
};

cbuffer Tint : register(b2)
{
    float4 gTint;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    // 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3). Covers the screen square with room to spare.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    const float2 position = corner * 2.0f - 1.0f;
    VertexOutput output;
    output.position = float4(position * gPlacement.xy + gPlacement.zw, 0.0f, 1.0f);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return gTint;
}
