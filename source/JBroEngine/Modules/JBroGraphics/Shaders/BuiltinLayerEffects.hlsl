// Layer effects (D-345, postprocess-plan section 3.12). They run on a layer's own texture before it is put
// down onto the view, so every input and output here holds premultiplied colour (see BuiltinLayerComposite.hlsl).
//   PSBlur              one direction of a separable Gaussian blur.
//   PSOutlineDistance   the distance across to the nearest covered texel (alpha >= 0.5), divided by 64.
//   PSOutlineComposite  the nearest covered texel down the column from those distances; paints the ring under the layer.
//                       A texel `width` texels away is fully covered; the edge fades over the next texel.
//   PSShadowBlur        the layer's alpha moved by the offset and blurred across.
//   PSShadowComposite   that blurred down and laid under the layer in the shadow colour.
//
//   b1  pixel stage, four float4 (see each pass).
//   t0  the input, read with Load. t1 the layer itself (the two composites).
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer LayerEffectParams : register(b1)
{
    float4 gParams[4];
};

Texture2D gInput : register(t0);
Texture2D gLayer : register(t1);

static const int MaxTaps = 33;
static const float DistanceScale = 64.0f;

int2 ClampPixel(Texture2D source, int2 pixel)
{
    uint width = 0;
    uint height = 0;
    source.GetDimensions(width, height);
    return clamp(pixel, int2(0, 0), int2((int)width - 1, (int)height - 1));
}

float4 LoadAt(Texture2D source, int2 pixel)
{
    return source.Load(int3(ClampPixel(source, pixel), 0));
}

float Gaussian(int step, float sigma)
{
    return exp(-(float)(step * step) / (2.0f * sigma * sigma));
}

// Lays `under` (premultiplied) beneath `over` (premultiplied).
float4 Under(float4 over, float4 under)
{
    return over + under * (1.0f - over.a);
}

// params[0]: direction (xy), radius in taps (z), sigma (w).
float4 PSBlur(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const int2 direction = int2(gParams[0].xy);
    const int radius = min((int)gParams[0].z, MaxTaps);
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weights = 0.0f;
    [loop]
    for (int step = -radius; step <= radius; ++step)
    {
        const float weight = Gaussian(step, gParams[0].w);
        sum += LoadAt(gInput, pixel + direction * step) * weight;
        weights += weight;
    }
    return sum / max(weights, 1e-6f);
}

// params[0]: reach in texels (x).
float4 PSOutlineDistance(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const int reach = min((int)gParams[0].x, MaxTaps);
    float nearest = DistanceScale;
    [loop]
    for (int step = -reach; step <= reach; ++step)
    {
        if (LoadAt(gInput, pixel + int2(step, 0)).a >= 0.5f)
        {
            nearest = min(nearest, (float)abs(step));
        }
    }
    const float value = nearest / DistanceScale;
    return float4(value, value, value, value);
}

// params[0]: width in pixels (x), reach in texels (y). params[1]: the colour (straight alpha).
float4 PSOutlineComposite(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const int reach = min((int)gParams[0].y, MaxTaps);
    float nearest = DistanceScale;
    [loop]
    for (int step = -reach; step <= reach; ++step)
    {
        const float across = LoadAt(gInput, pixel + int2(0, step)).r * DistanceScale;
        nearest = min(nearest, sqrt(across * across + (float)(step * step)));
    }
    const float coverage = saturate(gParams[0].x + 1.0f - nearest) * gParams[1].a;
    const float4 ring = float4(gParams[1].rgb * coverage, coverage);
    return Under(gLayer.Load(int3(pixel, 0)), ring);
}

// params[0]: offset in texels (xy, already turned to texture rows), radius in taps (z), sigma (w).
float4 PSShadowBlur(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy) - int2(gParams[0].xy);
    const int radius = min((int)gParams[0].z, MaxTaps);
    float sum = 0.0f;
    float weights = 0.0f;
    [loop]
    for (int step = -radius; step <= radius; ++step)
    {
        const float weight = Gaussian(step, gParams[0].w);
        sum += LoadAt(gInput, pixel + int2(step, 0)).a * weight;
        weights += weight;
    }
    const float alpha = sum / max(weights, 1e-6f);
    return float4(alpha, alpha, alpha, alpha);
}

// params[0]: radius in taps (z), sigma (w). params[1]: the colour (straight alpha).
float4 PSShadowComposite(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const int radius = min((int)gParams[0].z, MaxTaps);
    float sum = 0.0f;
    float weights = 0.0f;
    [loop]
    for (int step = -radius; step <= radius; ++step)
    {
        const float weight = Gaussian(step, gParams[0].w);
        sum += LoadAt(gInput, pixel + int2(0, step)).a * weight;
        weights += weight;
    }
    const float alpha = sum / max(weights, 1e-6f) * gParams[1].a;
    return Under(gLayer.Load(int3(pixel, 0)), float4(gParams[1].rgb * alpha, alpha));
}
