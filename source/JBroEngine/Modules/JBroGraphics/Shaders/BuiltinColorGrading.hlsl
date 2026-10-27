// Colour grading (D-343, postprocess-plan section 3.10). Both passes work on display values after the tonemap
// (or on an LDR scene) - the way image editors adjust colour.
//   PSAdjust  contrast around mid grey, colour filter, hue rotation, saturation (Unity's Color Adjustments without
//             post exposure - the Exposure effect does that before the tonemap).
//   PSLookup  a 3D LUT read with a trilinear sample, mixed with the input by contribution.
//
//   b1  pixel stage, four float4 (see each pass).
//   t0  the input, read with Load at this pixel. t1 the LUT (PSLookup). s0 a linear clamp sampler (PSLookup).
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer GradingParams : register(b1)
{
    float4 gParams[4];
};

Texture2D gInput : register(t0);
Texture3D gLut : register(t1);
SamplerState gLinear : register(s0);

float3 ToHsv(float3 c)
{
    const float maxValue = max(c.r, max(c.g, c.b));
    const float minValue = min(c.r, min(c.g, c.b));
    const float delta = maxValue - minValue;
    float hue = 0.0f;
    if (delta > 1e-6f)
    {
        if (maxValue == c.r)
        {
            hue = (c.g - c.b) / delta;
        }
        else if (maxValue == c.g)
        {
            hue = 2.0f + (c.b - c.r) / delta;
        }
        else
        {
            hue = 4.0f + (c.r - c.g) / delta;
        }
        hue = frac(hue / 6.0f);
    }
    const float saturation = maxValue > 1e-6f ? delta / maxValue : 0.0f;
    return float3(hue, saturation, maxValue);
}

float3 FromHsv(float3 hsv)
{
    const float3 k = saturate(abs(frac(hsv.x + float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
    return hsv.z * lerp(float3(1.0f, 1.0f, 1.0f), k, hsv.y);
}

// params[0]: contrast factor, saturation factor, hue shift in turns. params[1]: colour filter (rgb).
float4 PSAdjust(float4 position : SV_POSITION) : SV_TARGET
{
    const float4 input = gInput.Load(int3(position.xy, 0));
    float3 c = saturate(input.rgb);
    c = saturate((c - 0.5f) * gParams[0].x + 0.5f);
    c *= gParams[1].rgb;
    float3 hsv = ToHsv(c);
    hsv.x = frac(hsv.x + gParams[0].z);
    c = FromHsv(hsv);
    const float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    c = saturate(lerp(float3(luma, luma, luma), c, gParams[0].y));
    return float4(c, input.a);
}

// params[0]: contribution (x), LUT size (y). params[1]: domain min (rgb). params[2]: domain max (rgb).
float4 PSLookup(float4 position : SV_POSITION) : SV_TARGET
{
    const float4 input = gInput.Load(int3(position.xy, 0));
    const float size = gParams[0].y;
    const float3 x = saturate((input.rgb - gParams[1].rgb) / max(gParams[2].rgb - gParams[1].rgb, 1e-6f));
    // Texel centres: 0 lands on the first texel, 1 on the last.
    const float3 uvw = x * ((size - 1.0f) / size) + 0.5f / size;
    const float3 graded = gLut.SampleLevel(gLinear, uvw, 0.0f).rgb;
    return float4(lerp(input.rgb, graded, gParams[0].x), input.a);
}
