// Probes RHI compute: storage textures, structured buffers, reading one mip while writing another. The engine does
// not use it. Each entry point becomes its own compute pipeline; every pipeline declares only what its entry reads.
//   b1  gColor and gCount (how many values the buffer holds).
//   t0  a texture read by CSHalve.
//   u0  the storage texture written.
//   u4  the structured buffer (u4 onwards, whatever the texture count - BindingLayoutDesc in RHI.h).
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer Params : register(b1)
{
    float4 gColor;
    uint4 gCount;
};

Texture2D gInput : register(t0);
RWTexture2D<float4> gOutput : register(u0);
RWStructuredBuffer<float4> gValues : register(u4);

// Paints every texel of u0 with gColor.
[numthreads(8, 8, 1)]
void CSFill(uint3 id : SV_DispatchThreadID)
{
    gOutput[id.xy] = gColor;
}

// values[i] = (i / (n - 1), 1 - i / (n - 1), gColor.z, 1).
[numthreads(64, 1, 1)]
void CSWriteValues(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount.x)
    {
        return;
    }
    const float t = (float)id.x / (float)(gCount.x - 1);
    gValues[id.x] = float4(t, 1.0f - t, gColor.z, 1.0f);
}

// Each column of u0 becomes the value of the same index: output[x, y] = values[x].
[numthreads(8, 8, 1)]
void CSReadValues(uint3 id : SV_DispatchThreadID)
{
    gOutput[id.xy] = gValues[id.x];
}

// Writes a half-size mip: output[p] = input[2p] * gColor. Bloom's compute down chain has this shape.
[numthreads(8, 8, 1)]
void CSHalve(uint3 id : SV_DispatchThreadID)
{
    gOutput[id.xy] = gInput.Load(int3(id.xy * 2, 0)) * gColor;
}
