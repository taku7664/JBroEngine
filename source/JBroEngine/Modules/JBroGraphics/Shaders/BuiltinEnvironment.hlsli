// Shared by BuiltinMesh.hlsl and BuiltinSky.hlsl (D-447): how a direction finds its texel in the canvas environment's
// sky map. The C++ side (EnvironmentMap.cpp) bakes the map, its mips and its SH with the same convention - change both.
//
// The map is equirectangular: u runs once round +Y starting behind the viewer of the default camera (-Z is u = 0.5,
// +X is u = 0.75), v runs from straight up (0) to straight down (1).

#ifndef JBRO_BUILTIN_ENVIRONMENT_HLSLI
#define JBRO_BUILTIN_ENVIRONMENT_HLSLI

static const float EnvironmentPi = 3.14159265f;

// The sky turns by the rotation; looking up a direction in the turned sky turns the direction back.
float3 RotateIntoSky(float3 direction, float cosine, float sine)
{
    return float3(cosine * direction.x + sine * direction.z, direction.y, -sine * direction.x + cosine * direction.z);
}

float2 SkyMapUv(float3 direction)
{
    const float u = atan2(direction.x, -direction.z) * (0.5f / EnvironmentPi) + 0.5f;
    const float v = acos(clamp(direction.y, -1.0f, 1.0f)) / EnvironmentPi;
    return float2(u, v);
}

float3 EncodeSrgb(float3 colour)
{
    const float3 clamped = saturate(colour);
    const float3 low = clamped * 12.92f;
    const float3 high = 1.055f * pow(clamped, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(clamped, float3(0.0031308f, 0.0031308f, 0.0031308f)));
}

#endif
