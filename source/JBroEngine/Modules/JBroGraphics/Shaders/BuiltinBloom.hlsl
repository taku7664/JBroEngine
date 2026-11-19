// Bloom (D-343, postprocess-plan section 3.10): a raster mip chain (user's choice "raster mip chain").
// The scene is display values (see BuiltinPostProcess.hlsl); bloom works in linear light and lives in RGBA16F mips:
//   PSPrefilter   scene -> mip 0 (half size): decode, keep what is brighter than the threshold (soft knee), clamp.
//   PSDownsample  mip i -> mip i+1: Jimenez's 13 tap filter (Call of Duty: Advanced Warfare, SIGGRAPH 2014).
//   PSUpsample    mip i and the blurred mip i+1 -> blurred mip i: a 9 tap tent on the smaller one, mixed by scatter.
//   PSComposite   scene + blurred mip 0 * intensity * tint, encoded back to display values (above 1 stays above 1).
// Every pass draws its whole output; the uv of a pixel is its centre over the output size.
//
//   b1  pixel stage, four float4 (see each pass).
//   t0  the input. t1 the second input (upsample, composite). s0 a linear clamp sampler.
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer BloomParams : register(b1)
{
    float4 gParams[4];
};

Texture2D gInput : register(t0);
Texture2D gSecond : register(t1);
SamplerState gLinear : register(s0);

float3 Decode(float3 display)
{
    return pow(max(display, 0.0f), 2.2f);
}

float3 EncodeUnclamped(float3 linearLight)
{
    return pow(max(linearLight, 0.0f), 1.0f / 2.2f);
}

// params[0]: threshold, knee, clamp, 0. params[1]: the view's rectangle in the scene's uv (min xy, max xy).
// params[2]: 1 / output size (xy).
float4 PSPrefilter(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * gParams[2].xy;
    // Clamp to the view: outside it the scene holds another view or nothing.
    const float2 clamped = clamp(uv, gParams[1].xy, gParams[1].zw);
    const float3 colour = Decode(gInput.SampleLevel(gLinear, clamped, 0.0f).rgb);
    const float threshold = gParams[0].x;
    const float knee = gParams[0].y;
    const float brightness = max(colour.r, max(colour.g, colour.b));
    float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1e-4f);
    const float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4f);
    return float4(min(colour * contribution, gParams[0].zzz), 1.0f);
}

// params[0]: 1 / input size (xy). params[2]: 1 / output size (xy).
float4 PSDownsample(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * gParams[2].xy;
    const float2 t = gParams[0].xy;
    const float3 a = gInput.SampleLevel(gLinear, uv + t * float2(-2.0f, -2.0f), 0.0f).rgb;
    const float3 b = gInput.SampleLevel(gLinear, uv + t * float2(0.0f, -2.0f), 0.0f).rgb;
    const float3 c = gInput.SampleLevel(gLinear, uv + t * float2(2.0f, -2.0f), 0.0f).rgb;
    const float3 d = gInput.SampleLevel(gLinear, uv + t * float2(-2.0f, 0.0f), 0.0f).rgb;
    const float3 e = gInput.SampleLevel(gLinear, uv, 0.0f).rgb;
    const float3 f = gInput.SampleLevel(gLinear, uv + t * float2(2.0f, 0.0f), 0.0f).rgb;
    const float3 g = gInput.SampleLevel(gLinear, uv + t * float2(-2.0f, 2.0f), 0.0f).rgb;
    const float3 h = gInput.SampleLevel(gLinear, uv + t * float2(0.0f, 2.0f), 0.0f).rgb;
    const float3 i = gInput.SampleLevel(gLinear, uv + t * float2(2.0f, 2.0f), 0.0f).rgb;
    const float3 j = gInput.SampleLevel(gLinear, uv + t * float2(-1.0f, -1.0f), 0.0f).rgb;
    const float3 k = gInput.SampleLevel(gLinear, uv + t * float2(1.0f, -1.0f), 0.0f).rgb;
    const float3 l = gInput.SampleLevel(gLinear, uv + t * float2(-1.0f, 1.0f), 0.0f).rgb;
    const float3 m = gInput.SampleLevel(gLinear, uv + t * float2(1.0f, 1.0f), 0.0f).rgb;
    // The five boxes weigh 0.5 (centre) and 0.125 each (corners): e is in four boxes, the edges in two, the corners in one.
    float3 result = e * 0.125f;
    result += (a + c + g + i) * 0.03125f;
    result += (b + d + f + h) * 0.0625f;
    result += (j + k + l + m) * 0.125f;
    return float4(result, 1.0f);
}

// t0: mip i, the size of the output. t1: the blurred mip i+1. params[0]: 1 / the size of t1 (xy), scatter (z).
// params[2]: 1 / output size (xy).
float4 PSUpsample(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * gParams[2].xy;
    const float2 t = gParams[0].xy;
    float3 low = gSecond.SampleLevel(gLinear, uv + t * float2(-1.0f, -1.0f), 0.0f).rgb;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(0.0f, -1.0f), 0.0f).rgb * 2.0f;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(1.0f, -1.0f), 0.0f).rgb;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(-1.0f, 0.0f), 0.0f).rgb * 2.0f;
    low += gSecond.SampleLevel(gLinear, uv, 0.0f).rgb * 4.0f;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(1.0f, 0.0f), 0.0f).rgb * 2.0f;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(-1.0f, 1.0f), 0.0f).rgb;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(0.0f, 1.0f), 0.0f).rgb * 2.0f;
    low += gSecond.SampleLevel(gLinear, uv + t * float2(1.0f, 1.0f), 0.0f).rgb;
    low *= 1.0f / 16.0f;
    const float3 high = gInput.SampleLevel(gLinear, uv, 0.0f).rgb;
    return float4(lerp(high, low, gParams[0].z), 1.0f);
}

// t0: the scene, read with Load at this pixel. t1: the blurred mip 0.
// params[0]: intensity (x). params[1]: tint (rgb). params[2]: 1 / scene size (xy).
float4 PSComposite(float4 position : SV_POSITION) : SV_TARGET
{
    const float4 scene = gInput.Load(int3(position.xy, 0));
    const float2 uv = position.xy * gParams[2].xy;
    const float3 bloom = gSecond.SampleLevel(gLinear, uv, 0.0f).rgb;
    const float3 lit = Decode(scene.rgb) + bloom * gParams[0].x * gParams[1].rgb;
    return float4(EncodeUnclamped(lit), scene.a);
}
