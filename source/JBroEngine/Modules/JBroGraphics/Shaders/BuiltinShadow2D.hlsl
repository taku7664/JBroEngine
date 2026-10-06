// 2D shadow mask (D-291, tasks/lighting2d-plan.md step 3).
// Before a shadow-casting light is added to the light map, its mask is cleared to zero and every shadow edge of the
// view is pushed away from the light: one instance per edge, the renderer's unit quad picking the edge's two ends
// (x) and their near or far copy (y). The far copy lies twice the light's outer radius further along the ray from the
// light, beyond anything the light can reach. Closed outlines wind counter-clockwise, so an edge's outward normal is
// (dy, -dx); an edge that faces the light is not pushed unless the edge asks for it (selfShadow, or a two-sided chain),
// which leaves the caster's own inside lit. The mask is RGBA8 and the quads add One/One, so overlaps stay at one.
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
#if defined(JBRO_SPIRV)
struct ShadowConstantsBlock
{
    row_major float4x4 viewProjection;
    // xy: the light's world position, z: how far the far copy goes. w unused.
    float4 light;
};
[[vk::push_constant]] ConstantBuffer<ShadowConstantsBlock> gPush;
#define gViewProjection gPush.viewProjection
#define gLight gPush.light
#else
cbuffer ShadowConstants : register(b0)
{
    row_major float4x4 gViewProjection;
    float4 gLight;
};
#endif

// Mirrors GpuShadowEdgeInstance in Renderer.h.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    // xy: from, zw: to (world).
    float4 edge : ATTRIBUTE1;
    // x: one if the edge is pushed even when it faces the light.
    float4 flags : ATTRIBUTE2;
};

float4 VSMain(VertexInput input) : SV_POSITION
{
    const float2 from = input.edge.xy;
    const float2 to = input.edge.zw;
    const float2 along = to - from;
    const float2 outward = float2(along.y, -along.x);
    const bool facesLight = dot(outward, gLight.xy - from) > 0.0f;
    if (facesLight && input.flags.x < 0.5f)
    {
        // Every corner on the same point: the quad has no area and draws nothing.
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const float2 end = input.position.x < 0.0f ? from : to;
    const float2 ray = end - gLight.xy;
    const float rayLength = length(ray);
    const float2 direction = rayLength > 0.00001f ? ray / rayLength : float2(0.0f, 0.0f);
    const float2 world = input.position.y < 0.0f ? end : end + direction * gLight.z;
    return mul(gViewProjection, float4(world, 0.0f, 1.0f));
}

float4 PSMain() : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
