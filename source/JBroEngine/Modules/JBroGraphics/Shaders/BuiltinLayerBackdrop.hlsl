// Layer composite that reads what is below (D-283).
// The blends from Subtract on cannot be written as fixed blend factors, so the renderer copies the target
// into a backdrop texture first and this pass writes the mixed colour back over the view, opaquely.
// The layer texture holds premultiplied colour (see BuiltinLayerComposite.hlsl). The formulas are the
// separable blend modes of the W3C compositing spec, the ones Photoshop uses:
//   result = (1 - a) * Cb + a * B(Cb, Cs),  a = layer alpha * opacity,  Cs = unpremultiplied layer colour.
// Subtract is Photoshop's max(Cb - Cs, 0). Where the layer is empty (a = 0) the backdrop is written back unchanged.
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
#if defined(JBRO_SPIRV)
struct LayerBackdropBlock
{
    // x: opacity. y: mode, 0 = Subtract .. 8 = Difference. zw unused.
    float4 params;
};
[[vk::push_constant]] ConstantBuffer<LayerBackdropBlock> gPush;
#define gParams gPush.params
#else
cbuffer LayerBackdropConstants : register(b0)
{
    float4 gParams;
};
#endif

// t0: the layer, premultiplied. t1: the copy of the target taken just before this pass.
// Texels are read with Load, so the sampler is declared for the pipeline layout and never used.
Texture2D gLayer : register(t0);
Texture2D gBackdrop : register(t1);
SamplerState gSampler : register(s0);

float3 Screen(float3 b, float3 s)
{
    return b + s - b * s;
}

float3 HardLight(float3 b, float3 s)
{
    return float3(
        s.x <= 0.5f ? b.x * 2.0f * s.x : Screen(b, 2.0f * s - 1.0f).x,
        s.y <= 0.5f ? b.y * 2.0f * s.y : Screen(b, 2.0f * s - 1.0f).y,
        s.z <= 0.5f ? b.z * 2.0f * s.z : Screen(b, 2.0f * s - 1.0f).z);
}

float SoftLightChannel(float b, float s)
{
    if (s <= 0.5f)
    {
        return b - (1.0f - 2.0f * s) * b * (1.0f - b);
    }
    const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : sqrt(b);
    return b + (2.0f * s - 1.0f) * (d - b);
}

float DodgeChannel(float b, float s)
{
    if (b <= 0.0f)
    {
        return 0.0f;
    }
    if (s >= 1.0f)
    {
        return 1.0f;
    }
    return min(1.0f, b / (1.0f - s));
}

float BurnChannel(float b, float s)
{
    if (b >= 1.0f)
    {
        return 1.0f;
    }
    if (s <= 0.0f)
    {
        return 0.0f;
    }
    return 1.0f - min(1.0f, (1.0f - b) / s);
}

float3 Blend(int mode, float3 b, float3 s)
{
    switch (mode)
    {
    case 0:
        return max(b - s, 0.0f);
    case 1:
        return max(b, s);
    case 2:
        return min(b, s);
    case 3:
        return HardLight(s, b);
    case 4:
        return float3(SoftLightChannel(b.x, s.x), SoftLightChannel(b.y, s.y), SoftLightChannel(b.z, s.z));
    case 5:
        return HardLight(b, s);
    case 6:
        return float3(DodgeChannel(b.x, s.x), DodgeChannel(b.y, s.y), DodgeChannel(b.z, s.z));
    case 7:
        return float3(BurnChannel(b.x, s.x), BurnChannel(b.y, s.y), BurnChannel(b.z, s.z));
    default:
        return abs(b - s);
    }
}

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    const int3 texel = int3(int2(position.xy), 0);
    const float4 layer = gLayer.Load(texel);
    const float4 backdrop = gBackdrop.Load(texel);
    const float alpha = saturate(layer.a * gParams.x);
    const float3 source = layer.a > 0.0f ? saturate(layer.rgb / layer.a) : float3(0.0f, 0.0f, 0.0f);
    const float3 mixed = Blend((int)(gParams.y + 0.5f), backdrop.rgb, source);
    return float4(lerp(backdrop.rgb, mixed, alpha), alpha + backdrop.a * (1.0f - alpha));
}
