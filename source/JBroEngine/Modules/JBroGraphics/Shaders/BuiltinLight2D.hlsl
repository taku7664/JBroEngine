// 2D light (D-291, tasks/lighting2d-plan.md section 2.2).
// Point and spot lights are added into the view's light map (RGBA16F, cleared to the sum of the global lights) with
// One/One blending, one instance per light. The quad is the renderer's unit quad scaled to cover the light's outer
// radius around its world position, so a light costs only the pixels it can reach. Sprites on lit layers read the
// light map at their own pixel and multiply (BuiltinSprite.hlsl PSLitMain).
//
// b0 is the RHI's push-constant block; see BuiltinSprite.hlsl for the two spellings.
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

// Mirrors GpuLight2DInstance in Renderer.h.
struct VertexInput
{
    float2 position : ATTRIBUTE0;
    // xy: world centre, z: outer radius, w: inner radius (full strength inside it).
    float4 shape : ATTRIBUTE1;
    // rgb: colour times intensity, may exceed one.
    float4 color : ATTRIBUTE2;
    // xy: the spot's unit axis in world space, z: half the inner angle, w: half the outer angle (radians).
    // A point light sends z = 4 and w = 5, more than any angle from the axis can be.
    float4 cone : ATTRIBUTE3;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 offset : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float4 color : TEXCOORD2;
    float4 cone : TEXCOORD3;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float2 offset = input.position * (2.0f * input.shape.z);
    output.position = mul(gViewProjection, float4(input.shape.xy + offset, 0.0f, 1.0f));
    output.offset = offset;
    output.shape = input.shape;
    output.color = input.color;
    output.cone = input.cone;
    return output;
}

float4 Shade(VertexOutput input)
{
    const float distanceToLight = length(input.offset);
    const float outer = input.shape.z;
    const float inner = input.shape.w;
    // Full strength inside the inner radius, a smooth fall to nothing at the outer radius.
    const float radial = smoothstep(0.0f, 1.0f, saturate((outer - distanceToLight) / max(outer - inner, 0.0001f)));
    const float2 direction = distanceToLight > 0.00001f ? input.offset / distanceToLight : input.cone.xy;
    // The fall-off runs in angle, not in its cosine: halfway between the two angles is half the light.
    const float angle = acos(clamp(dot(direction, input.cone.xy), -1.0f, 1.0f));
    const float angular = 1.0f - smoothstep(0.0f, 1.0f, saturate((angle - input.cone.z) / max(input.cone.w - input.cone.z, 0.0001f)));
    // Alpha stays zero: the light map's alpha is not read, and One/One must not grow it.
    return float4(input.color.rgb * (radial * angular), 0.0f);
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return Shade(input);
}

// A shadow-casting light is drawn alone, after its shadow mask (BuiltinShadow2D.hlsl): where the mask is set the
// light does not reach. The mask is the size of the target and was drawn with the same viewport.
Texture2D gShadowMask : register(t0);

float4 PSShadowedMain(VertexOutput input) : SV_TARGET
{
    const float shadow = saturate(gShadowMask.Load(int3(int2(input.position.xy), 0)).r);
    return Shade(input) * (1.0f - shadow);
}
