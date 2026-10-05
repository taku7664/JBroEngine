// Pass 1 of the selection outline (D-276): mask -> scratch, grown across.
#include "BuiltinOutline.hlsli"

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    const float alpha = GrownAlpha(gFirst, int2(position.xy), int2(1, 0));
    return float4(alpha, alpha, alpha, alpha);
}
