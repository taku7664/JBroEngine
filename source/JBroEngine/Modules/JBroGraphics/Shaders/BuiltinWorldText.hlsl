// World text (D-218). The same unit quad as BuiltinSprite.hlsl (-0.5..0.5), placed in the world by a row-major 4x4
// matrix like BuiltinMesh.hlsl, so a glyph can face any direction. The renderer records it after the meshes of a view
// with the depth test on and depth writes off: meshes hide it, glyphs do not hide each other.
//
// params.y selects the shading per instance: 0 samples the texture as a sprite (the atlas is (1,1,1,coverage)),
// 1 reads the alpha as a signed distance field and composes fill and outline like BuiltinSdfText.hlsl.
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

// Mirrors GpuWorldTextInstance in Renderer.h.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    float4 worldRow0 : ATTRIBUTE1;
    float4 worldRow1 : ATTRIBUTE2;
    float4 worldRow2 : ATTRIBUTE3;
    float4 worldRow3 : ATTRIBUTE4;
    float4 fill : ATTRIBUTE5;
    float4 uvRect : ATTRIBUTE6;
    float4 outline : ATTRIBUTE7;
    // x: the distance value where the outline ends (0.5 means no outline). y: 1 for a distance field, 0 for a sprite.
    float4 params : ATTRIBUTE8;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 fill : COLOR0;
    float4 outline : COLOR1;
    float2 uv : TEXCOORD0;
    float2 params : TEXCOORD1;
};

VertexOutput VSMain(VertexInput input)
{
    const float4x4 world = float4x4(input.worldRow0, input.worldRow1, input.worldRow2, input.worldRow3);
    const float4 worldPosition = mul(world, float4(input.position, 0.0f, 1.0f));

    VertexOutput output;
    output.position = mul(gViewProjection, worldPosition);
    output.fill = input.fill;
    output.outline = input.outline;
    const float2 unit = float2(input.position.x + 0.5f, 0.5f - input.position.y);
    output.uv = unit * input.uvRect.zw + input.uvRect.xy;
    output.params = input.params.xy;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    const float4 texel = gTexture.Sample(gSampler, input.uv);
    if (input.params.y < 0.5f)
    {
        return texel * input.fill;
    }
    const float distanceValue = texel.a;
    const float smoothing = max(fwidth(distanceValue) * 0.5f, 1.0f / 1024.0f);
    const float fillCoverage = smoothstep(0.5f - smoothing, 0.5f + smoothing, distanceValue);
    const float outerEdge = max(min(input.params.x, 0.5f), smoothing);
    const float outerCoverage = smoothstep(outerEdge - smoothing, outerEdge + smoothing, distanceValue);
    const float outlineCoverage = max(outerCoverage - fillCoverage, 0.0f);

    const float fillAlpha = input.fill.a * fillCoverage;
    const float outlineAlpha = input.outline.a * outlineCoverage;
    const float alpha = saturate(fillAlpha + outlineAlpha);
    const float3 premultiplied = input.fill.rgb * fillAlpha + input.outline.rgb * outlineAlpha;
    const float3 color = alpha > 0.0001f ? premultiplied / alpha : float3(0.0f, 0.0f, 0.0f);
    return float4(color, alpha);
}
