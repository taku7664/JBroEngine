// Built-in post-process passes (D-342, postprocess-plan section 3.9).
// One full screen triangle from the vertex index, as in the test probes - no vertex buffer.
// Every pass reads its input with Load at its own pixel, so input and output are the same size and the
// view's viewport and scissor pick the pixels that are written.
//
// Colour space: the engine has always drawn display-encoded colour (textures and tints are UNORM, not sRGB),
// so the scene holds display values and an HDR scene holds display values above 1. Exposure and the tone
// curves work in linear light: they decode with a 2.2 power first and encode again after, so "None" leaves
// every value up to 1 as it was (lighting plan L4: white under a light of 1 stays white).
//
//   b1  pixel stage. params.x: exposure scale (PSExposure) or curve (PSTonemap: 0 None, 1 Reinhard, 2 ACES, 3 AgX).
//   t0  the input.
// ASCII only: fxc refuses a UTF-8 BOM and dxc refuses UTF-8 Korean without one.
cbuffer PostParams : register(b1)
{
    float4 gParams;
};

Texture2D gInput : register(t0);

float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION
{
    // 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3): one triangle over the whole target.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
}

float4 ReadInput(float4 position)
{
    return gInput.Load(int3(position.xy, 0));
}

// Copies the input as it is. Converts between formats (the target into an HDR scene, an HDR scene into the target).
float4 PSCopy(float4 position : SV_POSITION) : SV_TARGET
{
    return ReadInput(position);
}

// Scales the colour by 2^EV in linear light. The CPU passes the scale for display values: 2^(EV / 2.2).
float4 PSExposure(float4 position : SV_POSITION) : SV_TARGET
{
    const float4 colour = ReadInput(position);
    return float4(colour.rgb * gParams.x, colour.a);
}

float3 Decode(float3 display)
{
    return pow(max(display, 0.0f), 2.2f);
}

float3 Encode(float3 linearLight)
{
    return pow(saturate(linearLight), 1.0f / 2.2f);
}

float3 Reinhard(float3 x)
{
    return x / (1.0f + x);
}

// Krzysztof Narkowicz's fit of the ACES filmic curve.
float3 Aces(float3 x)
{
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

// AgX as Benjamin Wrensch's minimal version (the 6th order fit of Troy Sobotka's default contrast look).
// The rows below are the columns of the original GLSL matrices, so mul(v, M) is the GLSL M * v.
static const float3x3 AgxInset =
{
    0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
    0.0784335999999992f, 0.878468636469772f, 0.0784336f,
    0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f
};
static const float3x3 AgxOutset =
{
    1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
    -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
    -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f
};

float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

// Linear light in, display value out: AgX ends in the display encoding it was fitted for.
float3 AgxDisplay(float3 linearLight)
{
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    float3 v = mul(max(linearLight, 1e-10f), AgxInset);
    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = AgxContrast(v);
    return saturate(mul(v, AgxOutset));
}

float4 PSTonemap(float4 position : SV_POSITION) : SV_TARGET
{
    const float4 colour = ReadInput(position);
    const int curve = (int)gParams.x;
    float3 result;
    if (curve == 1)
    {
        result = Encode(Reinhard(Decode(colour.rgb)));
    }
    else if (curve == 2)
    {
        result = Encode(Aces(Decode(colour.rgb)));
    }
    else if (curve == 3)
    {
        result = AgxDisplay(Decode(colour.rgb));
    }
    else
    {
        result = saturate(colour.rgb);
    }
    return float4(result, saturate(colour.a));
}
