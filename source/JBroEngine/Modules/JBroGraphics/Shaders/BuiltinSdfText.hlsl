// SDF text (D-200 (4), text-plan section 4.5). Same unit quad and view constants as BuiltinSprite.hlsl, but the
// texture's alpha is a signed distance field (128 on the glyph edge, falling by 128/spread per atlas pixel outside)
// and the instance also carries an outline colour and the outline's edge value.
//
// Fill and outline are composed in one draw per glyph, so a translucent text does not darken where its own
// outline lies under its fill (the old engine did the same, Forward2DRenderer.cpp).
#if defined(JBRO_SPIRV)
struct ViewConstantsBlock
{
    row_major float4x4 viewProjection;
};
[[vk::push_constant]] ConstantBuffer<ViewConstantsBlock> gPush;
#define gViewProjection gPush.viewProjection
#else
cbuffer ViewConstants : register(b0)
{
    row_major float4x4 gViewProjection;
};
#endif

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

// Mirrors GpuTextInstance in Renderer.h.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float4 worldLinear : ATTRIBUTE1;
    float3 worldTranslation : ATTRIBUTE2;
    float4 fill : ATTRIBUTE3;
    float4 uvRect : ATTRIBUTE4;
    float4 outline : ATTRIBUTE5;
    // x: the distance value where the outline ends (0.5 means no outline). The CPU keeps it at least one atlas
    // pixel above zero, so the outline can never reach the empty corners of the quad.
    float4 params : ATTRIBUTE6;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 fill : COLOR0;
    float4 outline : COLOR1;
    float2 uv : TEXCOORD0;
    float outlineEdge : TEXCOORD1;
};

VertexOutput VSMain(VertexInput input)
{
    const float4 worldPosition = float4(
        dot(input.worldLinear.xy, input.position) + input.worldTranslation.x,
        dot(input.worldLinear.zw, input.position) + input.worldTranslation.y,
        input.worldTranslation.z,
        1.0f);

    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    output.fill = input.fill;
    output.outline = input.outline;
    const float2 unit = float2(input.position.x + 0.5f, 0.5f - input.position.y);
    output.uv = unit * input.uvRect.zw + input.uvRect.xy;
    output.outlineEdge = input.params.x;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    const float distanceValue = gTexture.Sample(gSampler, input.uv).a;
    // Half a screen pixel of distance on either side of an edge is the antialiasing band.
    const float smoothing = max(fwidth(distanceValue) * 0.5f, 1.0f / 1024.0f);
    const float fillCoverage = smoothstep(0.5f - smoothing, 0.5f + smoothing, distanceValue);
    // The outer edge never goes below the band: far away (zoomed out) the band widens, and an edge inside it
    // would light the whole quad.
    const float outerEdge = max(min(input.outlineEdge, 0.5f), smoothing);
    const float outerCoverage = smoothstep(outerEdge - smoothing, outerEdge + smoothing, distanceValue);
    const float outlineCoverage = max(outerCoverage - fillCoverage, 0.0f);

    const float fillAlpha = input.fill.a * fillCoverage;
    const float outlineAlpha = input.outline.a * outlineCoverage;
    const float alpha = saturate(fillAlpha + outlineAlpha);
    const float3 premultiplied = input.fill.rgb * fillAlpha + input.outline.rgb * outlineAlpha;
    const float3 color = alpha > 0.0001f ? premultiplied / alpha : float3(0.0f, 0.0f, 0.0f);
    return float4(color, alpha);
}
