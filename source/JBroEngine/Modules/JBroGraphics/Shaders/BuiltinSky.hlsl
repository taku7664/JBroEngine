// The sky background (D-447, lighting3d-plan §6): one triangle over the view reads the canvas environment's sky map
// (an equirectangular picture, mip 0) in the direction each pixel looks. It is the only draw in its own view, opened
// before the layer views, so it sits under every layer whatever their blends.
//
// The direction comes from the inverse view-projection: the near and far points under the pixel, so an orthographic
// camera sees the one direction it looks in. Output is sRGB-encoded like the mesh shader (UNORM targets).
//
// See BuiltinSprite.hlsl for why the push-constant block has two spellings.
#include "BuiltinEnvironment.hlsli"

#if defined(JBRO_SPIRV)
struct SkyConstantsBlock
{
    row_major float4x4 inverseViewProjection;
    // x cos and y sin of the sky's rotation about +Y, z the sky's intensity, w unused.
    float4 params;
};
[[vk::push_constant]] ConstantBuffer<SkyConstantsBlock> gPush;
#define gInverseViewProjection gPush.inverseViewProjection
#define gSkyParams gPush.params
#else
cbuffer SkyConstants : register(b0)
{
    row_major float4x4 gInverseViewProjection;
    float4 gSkyParams;
};
#endif

Texture2D gSkyMap : register(t0);
SamplerState gSkySampler : register(s0);

struct SkyVertex
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

// One triangle that covers the view: vertex ids 0, 1, 2 give (-1, -1), (3, -1), (-1, 3).
SkyVertex VSMain(uint id : SV_VertexID)
{
    const float2 corner = float2((id == 1) ? 3.0f : -1.0f, (id == 2) ? 3.0f : -1.0f);
    SkyVertex output;
    output.position = float4(corner, 0.0f, 1.0f);
    output.ndc = corner;
    return output;
}

float4 PSMain(SkyVertex input) : SV_TARGET
{
    const float4 nearPoint = mul(gInverseViewProjection, float4(input.ndc, 0.0f, 1.0f));
    const float4 farPoint = mul(gInverseViewProjection, float4(input.ndc, 1.0f, 1.0f));
    const float3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    const float3 turned = RotateIntoSky(direction, gSkyParams.x, gSkyParams.y);
    const float3 radiance = gSkyMap.SampleLevel(gSkySampler, SkyMapUv(turned), 0.0f).rgb * gSkyParams.z;
    return float4(EncodeSrgb(radiance), 1.0f);
}
