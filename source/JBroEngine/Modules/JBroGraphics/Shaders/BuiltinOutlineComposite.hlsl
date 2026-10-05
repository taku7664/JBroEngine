// Pass 2 of the selection outline (D-276): the scratch grown down, onto the view's target.
// Only the ring outside the mask is painted.
#include "BuiltinOutline.hlsli"

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float grown = GrownAlpha(gFirst, pixel, int2(0, 1));
    const float inside = gSecond.Load(int3(pixel, 0)).a;
    if (grown <= 0.5f || inside > 0.5f)
    {
        discard;
    }
    return gColor;
}
