// Probes RHI 3D textures and the sampler's W axis. The engine does not use it.
//
// Pairs with SubresourceProbe.hlsl's VSMain (same TEXCOORD0 output). b2.x is the w coordinate to sample at,
// so one draw reads one depth of the volume; s0's addressW decides what a w past 1 reads.
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer Lookup : register(b2)
{
    float4 gLookup;
};

Texture3D gVolume : register(t0);
SamplerState gSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return gVolume.SampleLevel(gSampler, float3(input.uv, gLookup.x), 0.0f);
}
