// Layer composite (D-279, the legacy Forward2DRenderer::CompositeLayer).
// A layer whose blend is not Normal, or whose opacity is below one, is first drawn into a scratch texture
// cleared to transparent black. Straight-alpha sprites blended onto that texture leave premultiplied colour
// in it, so this pass lays it onto the view's target with one of the Layer* blend states and does not
// multiply by alpha again. Opacity scales colour and alpha together, which keeps the texel premultiplied.
//
// The scratch texture is the size of the view's target and was drawn with the same viewport, so the
// texel under each pixel is read with Load at the pixel's own position. The vertex shader is the
// outline's (BuiltinOutlineGrow.hlsl): the renderer's unit quad doubled to cover the viewport.
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
#if defined(JBRO_SPIRV)
struct LayerCompositeBlock
{
    // x: opacity. yzw unused.
    float4 params;
};
[[vk::push_constant]] ConstantBuffer<LayerCompositeBlock> gPush;
#define gParams gPush.params
#else
cbuffer LayerCompositeConstants : register(b0)
{
    float4 gParams;
};
#endif

// Texels are read with Load, so the sampler is declared for the pipeline layout and never used.
Texture2D gLayer : register(t0);
SamplerState gSampler : register(s0);

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    return gLayer.Load(int3(int2(position.xy), 0)) * gParams.x;
}
