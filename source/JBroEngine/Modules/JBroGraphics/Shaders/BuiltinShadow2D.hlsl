// 2D shadow mask (D-291, tasks/lighting2d-plan.md steps 3 and 4).
// Before a shadow-casting light is added to the light map, its mask is cleared to zero and every shadow edge of the
// view is pushed away from the light: one instance per edge, the renderer's unit quad picking the edge's two ends
// (x) and their near or far copy (y). The far copy lies twice the light's outer radius further along the ray from the
// light, beyond anything the light can reach. Closed outlines wind counter-clockwise, so an edge's outward normal is
// (dy, -dx); an edge that faces the light is not pushed unless the edge asks for it (selfShadow, or a two-sided chain),
// which leaves the caster's own inside lit. The mask is RGBA16F and the quads add One/One; the light pass clamps it.
//
// Soft shadows (step 4): with a softness above zero the light is a disc of that radius. Each end is pushed along the
// line that touches the disc on the far side from the edge's other end, which widens the quad to the whole penumbra,
// and the pixel shader writes the share of the disc the edge hides as seen from the pixel: the overlap of the edge's
// angle span with the light's (atan(radius / distance) to either side of the direction to its centre), over the
// light's span. A convex outline's edges that face the pixel hide disjoint angle spans, so their shares add up to the
// whole of what the outline hides; a concave one may count a span twice and come out darker. At softness zero the
// pixel shader writes one and the quads are those of the hard shadow.
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
#if defined(JBRO_SPIRV)
struct ShadowConstantsBlock
{
    row_major float4x4 viewProjection;
    // xy: the light's world position, z: how far the far copy goes, w: the light's radius for soft shadows.
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

// The push constants reach the vertex stage only, so the pixel shader gets the edge and the light from here.
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 world : TEXCOORD0;
    nointerpolation float4 edge : TEXCOORD1;
    // xy: the light's world position, z: its radius.
    nointerpolation float4 light : TEXCOORD2;
};

float Cross2(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float2 from = input.edge.xy;
    const float2 to = input.edge.zw;
    const float radius = max(gLight.w, 0.0f);
    output.edge = input.edge;
    output.light = float4(gLight.xy, radius, 0.0f);
    const float2 along = to - from;
    const float2 outward = float2(along.y, -along.x);
    const bool facesLight = dot(outward, gLight.xy - from) > 0.0f;
    if (facesLight && input.flags.x < 0.5f)
    {
        // Every corner on the same point: the quad has no area and draws nothing.
        output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.world = float2(0.0f, 0.0f);
        return output;
    }
    const bool atFrom = input.position.x < 0.0f;
    const float2 end = atFrom ? from : to;
    const float2 other = atFrom ? to : from;
    const float2 ray = end - gLight.xy;
    const float rayLength = length(ray);
    float2 direction = rayLength > 0.00001f ? ray / rayLength : float2(0.0f, 0.0f);
    float reach = gLight.z;
    if (radius > 0.0f && rayLength > 0.00001f)
    {
        // Turn the ray away from the other end by the angle at which it touches the disc. An end inside the disc
        // turns by at most about 78 degrees, so the quad keeps its area.
        const float sine = min(radius / rayLength, 0.98f);
        const float cosine = sqrt(1.0f - sine * sine);
        const float side = Cross2(direction, other - end) > 0.0f ? -1.0f : 1.0f;
        direction = float2(cosine * direction.x - side * sine * direction.y, side * sine * direction.x + cosine * direction.y);
        // The far copy still has to clear the light's reach along the turned line.
        reach /= cosine;
    }
    const float2 world = input.position.y < 0.0f ? end : end + direction * reach;
    output.position = mul(gViewProjection, float4(world, 0.0f, 1.0f));
    output.world = world;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    const float radius = input.light.z;
    if (radius <= 0.0f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    const float2 from = input.edge.xy;
    const float2 to = input.edge.zw;
    const float2 centre = input.light.xy;
    const float2 pixel = input.world;
    // Only the side of the edge's line away from the light is behind the edge.
    const float2 along = to - from;
    if (Cross2(along, centre - from) * Cross2(along, pixel - from) >= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float2 toLight = centre - pixel;
    const float distance = length(toLight);
    const float2 axis = toLight / distance;
    const float halfSpan = atan(radius / distance);
    const float2 toFrom = from - pixel;
    const float2 toTo = to - pixel;
    // Both ends lie on the light's side of the pixel, so their angles to the axis stay within (-pi, pi) without wrapping.
    const float angleFrom = atan2(Cross2(axis, toFrom), dot(axis, toFrom));
    const float angleTo = atan2(Cross2(axis, toTo), dot(axis, toTo));
    const float low = max(min(angleFrom, angleTo), -halfSpan);
    const float high = min(max(angleFrom, angleTo), halfSpan);
    const float hidden = max(high - low, 0.0f) / (2.0f * halfSpan);
    return float4(hidden, hidden, hidden, hidden);
}
