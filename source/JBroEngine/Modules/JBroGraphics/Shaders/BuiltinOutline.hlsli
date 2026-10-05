// Selection outline for the editor's canvas view (D-276, the legacy COutlineRenderer2D).
// Shared by BuiltinOutlineGrow.hlsl and BuiltinOutlineComposite.hlsl. Each of those is one pass with its own
// PSMain - the Vulkan backend binds the entry points by the names VSMain and PSMain.
//
// The selected sprites are first drawn as they look into a mask texture. Two full-screen passes
// then grow that mask by `radius` pixels with a max of alpha - across in the first pass, down in
// the second - and the second pass paints `color` only where the grown mask covers and the mask
// itself does not. The line sits just outside the real pixels whatever the sprite's rotation,
// texture alpha or tint.
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
#if defined(JBRO_SPIRV)
struct OutlineConstantsBlock
{
    float4 color;
    // x: radius in pixels. yzw unused.
    float4 params;
};
[[vk::push_constant]] ConstantBuffer<OutlineConstantsBlock> gPush;
#define gColor gPush.color
#define gParams gPush.params
#else
cbuffer OutlineConstants : register(b0)
{
    float4 gColor;
    float4 gParams;
};
#endif

// t0: the texture this pass grows (the mask, then the across-grown mask).
// t1: the mask itself, read by the second pass only.
// Texels are read with Load, so the sampler is declared for the pipeline layout and never used.
Texture2D gFirst : register(t0);
Texture2D gSecond : register(t1);
SamplerState gSampler : register(s0);

// The renderer's unit quad runs -0.5..0.5; doubled it covers the whole target.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
};

float4 VSMain(VertexInput input) : SV_POSITION
{
    return float4(input.position * 2.0f, 0.0f, 1.0f);
}

static const int MaxRadius = 8;

float GrownAlpha(Texture2D source, int2 pixel, int2 direction)
{
    uint width = 0;
    uint height = 0;
    source.GetDimensions(width, height);
    const int radius = min((int)gParams.x, MaxRadius);
    float alpha = 0.0f;
    [unroll]
    for (int step = -MaxRadius; step <= MaxRadius; ++step)
    {
        if (abs(step) <= radius)
        {
            const int2 at = clamp(pixel + direction * step, int2(0, 0), int2((int)width - 1, (int)height - 1));
            alpha = max(alpha, source.Load(int3(at, 0)).a);
        }
    }
    return alpha;
}
