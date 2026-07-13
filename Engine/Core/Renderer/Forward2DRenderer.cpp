#include "pch.h"
#include "Forward2DRenderer.h"

#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/SpriteVulkanShaders.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/Logging/LoggerInternal.h"   // 진단 로그(CSystemLog)

#include <cfloat>
#include <cmath>
#include <format>
#include <utility>

namespace
{
	struct SpriteVertex
	{
		float Position[2];
		float Uv[2];
	};

	const char* SPRITE_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;
	float4 gViewRow1;
	float4 gUvRect;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 localPosition = float3(input.Position.xy, 1.0f);
	float2 worldPosition = float2(dot(localPosition, gTransformRow0.xyz), dot(localPosition, gTransformRow1.xyz));
	float3 worldPos3 = float3(worldPosition.x, worldPosition.y, 1.0f);
	output.Position = float4(dot(worldPos3, gViewRow0.xyz), dot(worldPos3, gViewRow1.xyz), 0.0f, 1.0f);
	output.Uv = input.Uv * gUvRect.zw + gUvRect.xy;
	output.Color = gColor;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	return gTexture.Sample(gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_SHADER_SOURCE_WGSL = R"(
struct SpriteConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
	UvRect : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Uv : vec2<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	let localPosition = vec3<f32>(input.Position.xy, 1.0);
	let worldPosition = vec2<f32>(dot(localPosition, gConstants.TransformRow0.xyz), dot(localPosition, gConstants.TransformRow1.xyz));
	let worldPos3 = vec3<f32>(worldPosition.x, worldPosition.y, 1.0);
	output.Position = vec4<f32>(dot(worldPos3, gConstants.ViewRow0.xyz), dot(worldPos3, gConstants.ViewRow1.xyz), 0.0, 1.0);
	output.Uv = input.Uv * gConstants.UvRect.zw + gConstants.UvRect.xy;
	output.Color = gConstants.Color;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	return textureSample(gTexture, gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_BATCH_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteViewConstants : register(b0)
{
	float4 gViewRow0;
	float4 gViewRow1;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD0;
	float4 TransformRow0 : TEXCOORD1;
	float4 TransformRow1 : TEXCOORD2;
	float4 UvRect : TEXCOORD3;
	float4 Color : COLOR0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 localPosition = float3(input.Position.xy, 1.0f);
	float2 worldPosition = float2(dot(localPosition, input.TransformRow0.xyz), dot(localPosition, input.TransformRow1.xyz));
	float3 worldPos3 = float3(worldPosition.x, worldPosition.y, 1.0f);
	output.Position = float4(dot(worldPos3, gViewRow0.xyz), dot(worldPos3, gViewRow1.xyz), 0.0f, 1.0f);
	output.Uv = input.Uv * input.UvRect.zw + input.UvRect.xy;
	output.Color = input.Color;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	return gTexture.Sample(gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_BATCH_SHADER_SOURCE_WGSL = R"(
struct SpriteViewConstants
{
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteViewConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Uv : vec2<f32>,
	@location(2) TransformRow0 : vec4<f32>,
	@location(3) TransformRow1 : vec4<f32>,
	@location(4) Color : vec4<f32>,
	@location(5) UvRect : vec4<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	let localPosition = vec3<f32>(input.Position.xy, 1.0);
	let worldPosition = vec2<f32>(dot(localPosition, input.TransformRow0.xyz), dot(localPosition, input.TransformRow1.xyz));
	let worldPos3 = vec3<f32>(worldPosition.x, worldPosition.y, 1.0);
	output.Position = vec4<f32>(dot(worldPos3, gConstants.ViewRow0.xyz), dot(worldPos3, gConstants.ViewRow1.xyz), 0.0, 1.0);
	output.Uv = input.Uv * input.UvRect.zw + input.UvRect.xy;
	output.Color = input.Color;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	return textureSample(gTexture, gSampler, input.Uv) * input.Color;
}
)";

	const char* TEXT_SHADER_SOURCE_HLSL = R"(
cbuffer TextConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gFillColor;
	float4 gViewRow0;
	float4 gViewRow1;
	float4 gOutlineColor;
	float4 gTextParams;
};
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);
struct VSIn { float2 Position : POSITION; float2 Uv : TEXCOORD0; };
struct VSOut { float4 Position : SV_POSITION; float2 Uv : TEXCOORD0; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 localPosition = float3(input.Position, 1.0f);
	float2 worldPosition = float2(dot(localPosition, gTransformRow0.xyz), dot(localPosition, gTransformRow1.xyz));
	float3 worldPos3 = float3(worldPosition, 1.0f);
	output.Position = float4(dot(worldPos3, gViewRow0.xyz), dot(worldPos3, gViewRow1.xyz), 0.0f, 1.0f);
	output.Uv = input.Uv;
	return output;
}
float4 PSMain(VSOut input) : SV_TARGET
{
	float distanceValue = gTexture.Sample(gSampler, input.Uv).r;
	float distancePerPixel = max(fwidth(distanceValue), 0.0001f);
	float smoothing = distancePerPixel;
	float fillCoverage = smoothstep(0.5f - smoothing, 0.5f + smoothing, distanceValue);
	float outerThreshold = 0.5f - distancePerPixel * max(gTextParams.x, 0.0f);
	float outerCoverage = smoothstep(outerThreshold - smoothing, outerThreshold + smoothing, distanceValue);
	float outlineCoverage = max(outerCoverage - fillCoverage, 0.0f);
	float fillAlpha = gFillColor.a * fillCoverage;
	float outlineAlpha = gOutlineColor.a * outlineCoverage;
	float alpha = saturate(fillAlpha + outlineAlpha);
	float3 premultiplied = gFillColor.rgb * fillAlpha + gOutlineColor.rgb * outlineAlpha;
	float3 color = alpha > 0.0001f ? premultiplied / alpha : float3(0.0f, 0.0f, 0.0f);
	return float4(color, alpha);
}
)";

	const char* TEXT_SHADER_SOURCE_WGSL = R"(
struct TextConstants {
	TransformRow0 : vec4<f32>, TransformRow1 : vec4<f32>, FillColor : vec4<f32>,
	ViewRow0 : vec4<f32>, ViewRow1 : vec4<f32>, OutlineColor : vec4<f32>, TextParams : vec4<f32>,
};
@group(0) @binding(0) var<uniform> gConstants : TextConstants;
@group(0) @binding(1) var gTexture : texture_2d<f32>;
@group(0) @binding(2) var gSampler : sampler;
struct VSIn { @location(0) Position : vec2<f32>, @location(1) Uv : vec2<f32>, };
struct VSOut { @builtin(position) Position : vec4<f32>, @location(0) Uv : vec2<f32>, };
@vertex fn VSMain(input : VSIn) -> VSOut {
	var output : VSOut;
	let localPosition = vec3<f32>(input.Position, 1.0);
	let worldPosition = vec2<f32>(dot(localPosition, gConstants.TransformRow0.xyz), dot(localPosition, gConstants.TransformRow1.xyz));
	let worldPos3 = vec3<f32>(worldPosition, 1.0);
	output.Position = vec4<f32>(dot(worldPos3, gConstants.ViewRow0.xyz), dot(worldPos3, gConstants.ViewRow1.xyz), 0.0, 1.0);
	output.Uv = input.Uv;
	return output;
}
@fragment fn PSMain(input : VSOut) -> @location(0) vec4<f32> {
	let distanceValue = textureSample(gTexture, gSampler, input.Uv).r;
	let distancePerPixel = max(fwidth(distanceValue), 0.0001);
	let smoothing = distancePerPixel;
	let fillCoverage = smoothstep(0.5 - smoothing, 0.5 + smoothing, distanceValue);
	let outerThreshold = 0.5 - distancePerPixel * max(gConstants.TextParams.x, 0.0);
	let outerCoverage = smoothstep(outerThreshold - smoothing, outerThreshold + smoothing, distanceValue);
	let outlineCoverage = max(outerCoverage - fillCoverage, 0.0);
	let fillAlpha = gConstants.FillColor.a * fillCoverage;
	let outlineAlpha = gConstants.OutlineColor.a * outlineCoverage;
	let alpha = clamp(fillAlpha + outlineAlpha, 0.0, 1.0);
	let premultiplied = gConstants.FillColor.rgb * fillAlpha + gConstants.OutlineColor.rgb * outlineAlpha;
	let color = select(vec3<f32>(0.0), premultiplied / alpha, alpha > 0.0001);
	return vec4<f32>(color, alpha);
}
)";

	// occluder 자기 footprint 채움 PS — 스프라이트 VS 와 페어. 스프라이트 알파를 그림자량으로 마스크에
	// Additive 출력(불투명 occluder 는 자기 자리의 빛도 막는다 → self-shadow). 엣지 페넘브라와 함께 누적.
	const char* OCCLUDERFILL_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;
	float4 gViewRow1;
	float4 gUvRect;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

float4 PSMain(VSOut input) : SV_TARGET
{
	float a = gTexture.Sample(gSampler, input.Uv).a * input.Color.a;
	return float4(a, a, a, a);
}
)";

	const char* OCCLUDERFILL_SHADER_SOURCE_WGSL = R"(
struct SpriteConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
	UvRect : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	let a = textureSample(gTexture, gSampler, input.Uv).a * input.Color.a;
	return vec4<f32>(a, a, a, a);
}
)";

	// 폴라 1D 섀도맵 리덕션 셰이더 — OccluderMap(월드공간, 카메라뷰로 그림)을 광원 중심 각도(θ=열)별
	// 최근접 오클루더까지의 정규 거리[0,1]로 축약. 풀스크린 VS(POSITION 직접 NDC) + 레이마치 PS(Opaque).
	// gViewRow0/1 = OccluderMap 을 그린 카메라 world→NDC(월드점 샘플 UV 재구성). gSecondaryColor=(lightX,lightY,range,_).
	const char* REDUCTION_SHADER_SOURCE_HLSL = R"(
cbuffer ReductionConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;         // camera world->ndc row0 (a,b,tx)
	float4 gViewRow1;         // camera world->ndc row1 (c,d,ty)
	float4 gUvRect;
	float4 gSecondaryColor;   // (lightX, lightY, lightRange, _)
	float4 gShaderParams;
	float4 gShadowParams;
};

Texture2D gTexture : register(t0);   // OccluderMap
SamplerState gSampler : register(s0);

struct VSIn
{
	float2 Position : POSITION;   // 로컬 쿼드 [-0.5,0.5]
	float2 Uv : TEXCOORD0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;        // uv.x = 열(각도 fraction)
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	output.Position = float4(input.Position * 2.0f, 0.0f, 1.0f);   // [-0.5,0.5] -> [-1,1] 풀스크린
	output.Uv = input.Uv;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	const int STEPS = 256;
	const float PI = 3.14159265f;
	float theta = input.Uv.x * (2.0f * PI) - PI;   // 열 -> [-PI, PI]
	float2 dir = float2(cos(theta), sin(theta));
	float2 lightPos = gSecondaryColor.xy;
	float range = gSecondaryColor.z;
	float result = 1.0f;   // 미차폐 = 최대 거리
	[loop] for (int i = 0; i < STEPS; ++i)
	{
		float t = (float(i) + 0.5f) / float(STEPS);
		float2 wp = lightPos + dir * (t * range);
		float3 w3 = float3(wp, 1.0f);
		float2 ndc = float2(dot(w3, gViewRow0.xyz), dot(w3, gViewRow1.xyz));
		float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
		if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
		{
			continue;   // 오클루더맵 뷰 밖 = 이 방향 이 거리엔 오클루더 없음
		}
		float a = gTexture.SampleLevel(gSampler, uv, 0.0f).r;
		if (a >= 0.5f)
		{
			result = t;
			break;
		}
	}
	return float4(result, result, result, 1.0f);
}
)";

	const char* REDUCTION_SHADER_SOURCE_WGSL = R"(
struct ReductionConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
	UvRect : vec4<f32>,
	SecondaryColor : vec4<f32>,
	ShaderParams : vec4<f32>,
	ShadowParams : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : ReductionConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Uv : vec2<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	output.Position = vec4<f32>(input.Position * 2.0, 0.0, 1.0);
	output.Uv = input.Uv;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	let PI = 3.14159265;
	let theta = input.Uv.x * (2.0 * PI) - PI;
	let dir = vec2<f32>(cos(theta), sin(theta));
	let lightPos = gConstants.SecondaryColor.xy;
	let range = gConstants.SecondaryColor.z;
	var result = 1.0;
	for (var i = 0; i < 256; i = i + 1)
	{
		let t = (f32(i) + 0.5) / 256.0;
		let wp = lightPos + dir * (t * range);
		let w3 = vec3<f32>(wp, 1.0);
		let ndc = vec2<f32>(dot(w3, gConstants.ViewRow0.xyz), dot(w3, gConstants.ViewRow1.xyz));
		let uv = vec2<f32>(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
		if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
		{
			let a = textureSampleLevel(gTexture, gSampler, uv, 0.0).r;
			if (a >= 0.5)
			{
				result = t;
				break;
			}
		}
	}
	return vec4<f32>(result, result, result, 1.0);
}
)";

	// 라이트 감쇠 PS — 스프라이트 VS(POSITION+TEXCOORD, SpriteConstants)와 페어. LightMap 에 Additive 누적.
	// gShaderParams.x = 타입(0=Directional 균일, 1=Point 반경 감쇠), .y = intensity, .zw = 렌더타겟 크기(px).
	// gTexture = 그림자 마스크(스크린공간, 무그림자 시 white). 라이트 기여 × (1 - mask).
	const char* LIGHT_SHADER_SOURCE_HLSL = R"(
cbuffer LightConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;
	float4 gViewRow1;
	float4 gUvRect;
	float4 gSecondaryColor;
	float4 gShaderParams;
	float4 gShadowParams;   // x=행(-1=무그림자), y=행수, z=softness01, w=예비
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

float4 PSMain(VSOut input) : SV_TARGET
{
	float type = gShaderParams.x;    // 0=Directional, 1=Point, 2=Spot
	float intensity = gShaderParams.y;
	float atten = 1.0f;
	if (type > 0.5f)
	{
		float2 d = (input.Uv - 0.5f) * 2.0f;
		float r = saturate(1.0f - length(d));
		atten = r * r;
		if (type > 1.5f)
		{
			// Spot: 각도 감쇠. gSecondaryColor.xy=월드 방향, .z=cos(inner/2), .w=cos(outer/2).
			// uv → 월드 방향(쿼드 uv.y 는 월드 -Y 방향이라 부호 반전).
			float2 pdir = float2(input.Uv.x - 0.5f, -(input.Uv.y - 0.5f));
			float pl = length(pdir);
			float2 pn = pl > 0.0001f ? pdir / pl : gSecondaryColor.xy;
			float cosA = dot(pn, gSecondaryColor.xy);
			float cone = smoothstep(gSecondaryColor.w, gSecondaryColor.z, cosA);
			atten *= cone;
		}
	}
	// 폴라 1D 섀도맵: ShadowParams.x = 이 라이트의 아틀라스 행(<0 = 무그림자). gTexture = ShadowAtlas.
	// (θ,r) 로 샘플 → step(r, dist) umbra, 각도축 9-tap 가우시안(폭 ∝ 거리 × softness) penumbra.
	float lit = 1.0f;
	float row = gShadowParams.x;
	if (row >= 0.0f)
	{
		const float PI = 3.14159265f;
		float2 dd = (input.Uv - 0.5f) * 2.0f;    // 라이트 쿼드 로컬 = 월드축 정렬(range 정규)
		float rNorm = length(dd);
		float theta = atan2(-dd.y, dd.x);         // uv.y 뒤집힘 보정 → 월드 각도[-PI,PI]
		float ax = (theta + PI) / (2.0f * PI);    // 리덕션 열 매핑의 역
		float ay = (row + 0.5f) / gShadowParams.y;
		float softness = gShadowParams.z;
		// softness01 [0,1] → 옛 blur 스케일 [0,0.3] 로 매핑(0.05×0.3=0.015). 옛 r 성장식.
		float blur = softness * 0.015f * smoothstep(0.0f, 1.0f, rNorm);
		float weights[5] = { 0.227f, 0.194f, 0.121f, 0.054f, 0.016f };
		float sum = 0.0f;
		float wsum = 0.0f;
		[unroll] for (int k = -4; k <= 4; ++k)
		{
			float axk = ax + (float(k) / 4.0f) * blur;
			float dist = gTexture.SampleLevel(gSampler, float2(axk, ay), 0.0f).r;
			float w = weights[abs(k)];
			sum += step(rNorm, dist) * w;
			wsum += w;
		}
		lit = sum / wsum;
	}
	return float4(input.Color.rgb * intensity * atten * lit, 1.0f);
}
)";

	const char* LIGHT_SHADER_SOURCE_WGSL = R"(
struct LightConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
	UvRect : vec4<f32>,
	SecondaryColor : vec4<f32>,
	ShaderParams : vec4<f32>,
	ShadowParams : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : LightConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	let type = gConstants.ShaderParams.x;   // 0=Directional, 1=Point, 2=Spot
	let intensity = gConstants.ShaderParams.y;
	var atten = 1.0;
	if (type > 0.5)
	{
		let d = (input.Uv - 0.5) * 2.0;
		let r = clamp(1.0 - length(d), 0.0, 1.0);
		atten = r * r;
		if (type > 1.5)
		{
			let pdir = vec2<f32>(input.Uv.x - 0.5, -(input.Uv.y - 0.5));
			let pl = length(pdir);
			var pn = gConstants.SecondaryColor.xy;
			if (pl > 0.0001) { pn = pdir / pl; }
			let cosA = dot(pn, gConstants.SecondaryColor.xy);
			let cone = smoothstep(gConstants.SecondaryColor.w, gConstants.SecondaryColor.z, cosA);
			atten = atten * cone;
		}
	}
	var lit = 1.0;
	let row = gConstants.ShadowParams.x;
	if (row >= 0.0)
	{
		let PI = 3.14159265;
		let dd = (input.Uv - 0.5) * 2.0;
		let rNorm = length(dd);
		let theta = atan2(-dd.y, dd.x);
		let ax = (theta + PI) / (2.0 * PI);
		let ay = (row + 0.5) / gConstants.ShadowParams.y;
		let softness = gConstants.ShadowParams.z;
		// softness01 [0,1] → 옛 blur 스케일 [0,0.3] 로 매핑(0.05×0.3=0.015). 옛 r 성장식.
		let blur = softness * 0.015 * smoothstep(0.0, 1.0, rNorm);
		var weights = array<f32, 5>(0.227, 0.194, 0.121, 0.054, 0.016);
		var sum = 0.0;
		var wsum = 0.0;
		for (var k = -4; k <= 4; k = k + 1)
		{
			let axk = ax + (f32(k) / 4.0) * blur;
			let dist = textureSampleLevel(gTexture, gSampler, vec2<f32>(axk, ay), 0.0).r;
			let w = weights[abs(k)];
			sum = sum + step(rNorm, dist) * w;
			wsum = wsum + w;
		}
		lit = sum / wsum;
	}
	return vec4<f32>(input.Color.rgb * intensity * atten * lit, 1.0);
}
)";

	// 톤맵 PS — HDR 컴포짓 결과(gTexture)를 Reinhard 로 롤오프해 RGBA8 로. 밝은 라이트 중심이
	// 하드 클램프(순백)되지 않고 부드럽게 포화. 스프라이트 VS + BuildViewportColorConstants(항등 uv) 사용.
	const char* TONEMAP_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;
	float4 gViewRow1;
	float4 gUvRect;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

float4 PSMain(VSOut input) : SV_TARGET
{
	float4 hdr = gTexture.Sample(gSampler, input.Uv);
	float3 mapped = hdr.rgb / (1.0f + hdr.rgb);   // Reinhard
	return float4(mapped, hdr.a);
}
)";

	const char* TONEMAP_SHADER_SOURCE_WGSL = R"(
struct SpriteConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
	UvRect : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	let hdr = textureSample(gTexture, gSampler, input.Uv);
	let mapped = hdr.rgb / (1.0 + hdr.rgb);
	return vec4<f32>(mapped, hdr.a);
}
)";
}

bool CForward2DRenderer::Initialize(const RendererDesc& desc)
{
	if (m_isInitialized)
	{
		return true;
	}

	m_rhiDevice = desc.RHIDevice;
	if (false == m_rhiDevice.IsValid())
	{
		return false;
	}

	if (false == CreateSpritePipeline() || false == CreateQuadMesh())
	{
		if (ERHIApi::D3D11 == m_rhiDevice->GetApi())
		{
			Finalize();
			return false;
		}

		m_isInitialized = true;
		return true;
	}

	CreateSpriteBatchPipeline();
	const bool blitOk = CreateBlitPipeline();
	const bool lightOk = CreateLightPipeline();
	const bool occluderFillOk = CreateOccluderFillPipeline();
	const bool reductionOk = CreatePolarReductionPipeline();
	const bool compositeOk = CreateCompositePipeline();
	const bool tonemapOk = CreateTonemapPipeline();
	CSystemLog::Info(std::format("[RenderWeave] pipelines blit={} light={} occluderMap={} reduction={} composite={} tonemap={}",
		blitOk, lightOk, occluderFillOk, reductionOk, compositeOk, tonemapOk));
	if (false == CreateTextPipeline() && ERHIApi::D3D11 == m_rhiDevice->GetApi())
	{
		Finalize();
		return false;
	}

	RHISamplerDesc samplerDesc;
	samplerDesc.Filter = ERHIFilterMode::Linear;
	samplerDesc.AddressU = ERHIAddressMode::Clamp;
	samplerDesc.AddressV = ERHIAddressMode::Clamp;
	m_defaultSampler = m_rhiDevice->CreateSampler(samplerDesc);

	// 1×1 white texture used by FillViewportColor.
	{
		const std::uint8_t whitePixel[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		RHITexture2DDesc whiteDesc;
		whiteDesc.Width     = 1;
		whiteDesc.Height    = 1;
		whiteDesc.Format    = ERHITextureFormat::RGBA8;
		whiteDesc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource);
		m_whiteTexture = m_rhiDevice->CreateTexture2D(whiteDesc, whitePixel);
	}

	m_weavePool.SetDevice(m_rhiDevice);

	m_isInitialized = true;
	return true;
}

void CForward2DRenderer::BeginFrame()
{
	m_spriteConstantBufferCursor = 0;
	m_spriteViewConstantBufferCursor = 0;
	m_spriteInstanceBufferCursor = 0;
	m_weavePool.BeginFrame();
}

void CForward2DRenderer::SetRenderTargetSize(const RenderSurfaceSize& size)
{
	m_renderTargetSize = size;
}

void CForward2DRenderer::SetViewCamera(float posX, float posY, float orthographicSize)
{
	m_viewCamX      = posX;
	m_viewCamY      = posY;
	m_viewCamSize   = orthographicSize > 0.0f ? orthographicSize : 1.0f;
	m_useExplicitSize = false;
}

void CForward2DRenderer::SetViewCameraEx(float posX, float posY, float halfW, float halfH, float cosR, float sinR)
{
	m_viewCamX      = posX;
	m_viewCamY      = posY;
	m_viewCamHalfW  = halfW > 0.0f ? halfW : 1.0f;
	m_viewCamHalfH  = halfH > 0.0f ? halfH : 1.0f;
	m_viewCamCosR   = cosR;
	m_viewCamSinR   = sinR;
	m_useExplicitSize = true;
}

void CForward2DRenderer::SetSurfacePreRotation(float cosR, float sinR)
{
	m_surfacePreRotCos = cosR;
	m_surfacePreRotSin = sinR;
	m_useExplicitSize = true;
}

void CForward2DRenderer::Render(IRenderScene& scene)
{
	RenderImpl(scene, nullptr);
}

RenderCullingStats CForward2DRenderer::GetLastCullingStats() const
{
	return m_lastCullingStats;
}

void CForward2DRenderer::RenderExcluding(IRenderScene& scene, const std::unordered_set<RenderObjectId>& excluded)
{
	RenderImpl(scene, &excluded);
}

CForward2DRenderer::ViewParameters CForward2DRenderer::BuildViewParameters() const
{
	ViewParameters view;
	if (m_useExplicitSize)
	{
		view.HalfW = m_viewCamHalfW;
		view.HalfH = m_viewCamHalfH;
		view.CosR = m_viewCamCosR;
		view.SinR = m_viewCamSinR;
		return view;
	}

	const float width = static_cast<float>(m_renderTargetSize.Width);
	const float height = static_cast<float>(m_renderTargetSize.Height);
	const float aspect = (width > 0.0f && height > 0.0f) ? width / height : 1.0f;
	const float size = m_viewCamSize > 0.0f ? m_viewCamSize : 1.0f;
	view.HalfW = size * aspect;
	view.HalfH = size;
	view.CosR = 1.0f;
	view.SinR = 0.0f;
	return view;
}

CForward2DRenderer::SpriteDrawResources CForward2DRenderer::ResolveSpriteDrawResources(const RenderItem& item) const
{
	SpriteDrawResources resources;
	resources.Mesh = item.Mesh;
	resources.Pipeline = item.Pipeline;
	resources.Texture = item.Texture;
	resources.Sampler = item.Sampler;

	if (item.Material.IsValid())
	{
		if (false == resources.Pipeline.IsValid())
		{
			resources.Pipeline = item.Material->GetPipeline();
		}
		if (false == resources.Texture.IsValid())
		{
			resources.Texture = item.Material->GetTexture();
		}
		if (false == resources.Sampler.IsValid())
		{
			resources.Sampler = item.Material->GetSampler();
		}
	}

	if (false == resources.Pipeline.IsValid())
	{
		resources.Pipeline = m_spritePipeline.GetSafePtr();
	}
	if (false == resources.Sampler.IsValid())
	{
		resources.Sampler = m_defaultSampler.GetSafePtr();
	}

	return resources;
}

CForward2DRenderer::SpriteConstants CForward2DRenderer::BuildSpriteConstants(
	const RenderItem& item,
	const ViewParameters& view) const
{
	SpriteConstants constants = {};
	constants.TransformRow0[0] = item.Transform.M11;
	constants.TransformRow0[1] = item.Transform.M21;
	constants.TransformRow0[2] = item.Transform.Dx;
	constants.TransformRow1[0] = item.Transform.M12;
	constants.TransformRow1[1] = item.Transform.M22;
	constants.TransformRow1[2] = item.Transform.Dy;
	for (int c = 0; c < 4; ++c)
	{
		constants.Color[c] = item.Color[c];
		constants.SecondaryColor[c] = item.SecondaryColor[c];
		constants.ShaderParams[c] = item.ShaderParams[c];
		constants.UvRect[c] = item.UvRect[c];
	}

	const float camX = m_viewCamX;
	const float camY = m_viewCamY;
	constants.ViewRow0[0] =  view.CosR / view.HalfW;
	constants.ViewRow0[1] =  view.SinR / view.HalfW;
	constants.ViewRow0[2] = -(view.CosR * camX + view.SinR * camY) / view.HalfW;
	constants.ViewRow1[0] = -view.SinR / view.HalfH;
	constants.ViewRow1[1] =  view.CosR / view.HalfH;
	constants.ViewRow1[2] =  (view.SinR * camX - view.CosR * camY) / view.HalfH;
	ApplySurfacePreRotation(constants.ViewRow0, constants.ViewRow1);
	return constants;
}

CForward2DRenderer::SpriteViewConstants CForward2DRenderer::BuildSpriteViewConstants(const ViewParameters& view) const
{
	SpriteViewConstants constants = {};
	const float camX = m_viewCamX;
	const float camY = m_viewCamY;
	constants.ViewRow0[0] =  view.CosR / view.HalfW;
	constants.ViewRow0[1] =  view.SinR / view.HalfW;
	constants.ViewRow0[2] = -(view.CosR * camX + view.SinR * camY) / view.HalfW;
	constants.ViewRow1[0] = -view.SinR / view.HalfH;
	constants.ViewRow1[1] =  view.CosR / view.HalfH;
	constants.ViewRow1[2] =  (view.SinR * camX - view.CosR * camY) / view.HalfH;
	ApplySurfacePreRotation(constants.ViewRow0, constants.ViewRow1);
	return constants;
}

void CForward2DRenderer::ApplySurfacePreRotation(float (&viewRow0)[4], float (&viewRow1)[4]) const
{
	// 최종 클립 좌표에 surface 회전 S=[c -s; s c] 를 곱한다(clip_final = S * (View * world)).
	const float c = m_surfacePreRotCos;
	const float s = m_surfacePreRotSin;
	for (int i = 0; i < 3; ++i)
	{
		const float r0 = viewRow0[i];
		const float r1 = viewRow1[i];
		viewRow0[i] = c * r0 - s * r1;
		viewRow1[i] = s * r0 + c * r1;
	}
}

CForward2DRenderer::SpriteInstanceData CForward2DRenderer::BuildSpriteInstanceData(const RenderItem& item) const
{
	SpriteInstanceData instance = {};
	instance.TransformRow0[0] = item.Transform.M11;
	instance.TransformRow0[1] = item.Transform.M21;
	instance.TransformRow0[2] = item.Transform.Dx;
	instance.TransformRow1[0] = item.Transform.M12;
	instance.TransformRow1[1] = item.Transform.M22;
	instance.TransformRow1[2] = item.Transform.Dy;
	for (int c = 0; c < 4; ++c)
	{
		instance.Color[c] = item.Color[c];
		instance.UvRect[c] = item.UvRect[c];
	}
	return instance;
}

CForward2DRenderer::SpriteConstants CForward2DRenderer::BuildViewportColorConstants(float r, float g, float b, float a) const
{
	SpriteConstants constants = {};
	constants.TransformRow0[0] = 2.0f;
	constants.TransformRow1[1] = 2.0f;
	constants.Color[0] = r;
	constants.Color[1] = g;
	constants.Color[2] = b;
	constants.Color[3] = a;
	constants.ViewRow0[0] = 1.0f;
	constants.ViewRow1[1] = 1.0f;
	// UvRect 항등 — 풀스크린 쿼드가 텍스처 전체를 샘플하도록(blit/composite). 미설정 시 uv=0 고정
	// → 코너 텍셀 하나로 전체가 채워진다(1×1 white 인 FillViewportColor 에선 무해했던 잠복 버그).
	constants.UvRect[2] = 1.0f;
	constants.UvRect[3] = 1.0f;
	return constants;
}

RWTexturePool& CForward2DRenderer::GetRenderWeavePool()
{
	return m_weavePool;
}

void CForward2DRenderer::BlitFullscreen(IRHICommandContext& commandContext, SafePtr<IRHITexture> src)
{
	// 전용 no-blend blit 파이프라인(Opaque)으로 src 를 풀스크린 복사. color=white,
	// UvRect 항등 → out = 텍스처. 없으면(생성 실패) 스프라이트 파이프라인 폴백.
	SafePtr<IRHIGraphicsPipeline> pipeline = m_blitPipeline ? m_blitPipeline.GetSafePtr() : m_spritePipeline.GetSafePtr();
	if (false == pipeline.IsValid() || !m_quadMesh || false == src.IsValid() || !m_defaultSampler)
	{
		return;
	}
	const SpriteConstants constants = BuildViewportColorConstants(1.0f, 1.0f, 1.0f, 1.0f);
	RenderStateCache stateCache;
	DrawSpriteQuad(
		commandContext,
		stateCache,
		pipeline,
		m_quadMesh.GetSafePtr(),
		src,
		m_defaultSampler.GetSafePtr(),
		constants);
}

CForward2DRenderer::SpriteConstants CForward2DRenderer::BuildLightConstants(
	int type, float worldX, float worldY, float range, const float color[4], float intensity, const ViewParameters& view) const
{
	SpriteConstants constants = {};
	if (0 == type)
	{
		// Directional — 뷰포트 균일. NDC 풀스크린 쿼드(Transform 2배, View 항등).
		constants.TransformRow0[0] = 2.0f;
		constants.TransformRow1[1] = 2.0f;
		constants.ViewRow0[0] = 1.0f;
		constants.ViewRow1[1] = 1.0f;
	}
	else
	{
		// Point — 월드 pos 중심, 변 길이 2*range 쿼드(로컬 [-0.5,0.5] → 반경 range). 카메라 뷰 적용.
		const float diameter = range > 0.0f ? range * 2.0f : 0.0f;
		constants.TransformRow0[0] = diameter;   // M11
		constants.TransformRow0[2] = worldX;     // Dx
		constants.TransformRow1[1] = diameter;   // M22
		constants.TransformRow1[2] = worldY;     // Dy
		const float camX = m_viewCamX;
		const float camY = m_viewCamY;
		constants.ViewRow0[0] =  view.CosR / view.HalfW;
		constants.ViewRow0[1] =  view.SinR / view.HalfW;
		constants.ViewRow0[2] = -(view.CosR * camX + view.SinR * camY) / view.HalfW;
		constants.ViewRow1[0] = -view.SinR / view.HalfH;
		constants.ViewRow1[1] =  view.CosR / view.HalfH;
		constants.ViewRow1[2] =  (view.SinR * camX - view.CosR * camY) / view.HalfH;
		ApplySurfacePreRotation(constants.ViewRow0, constants.ViewRow1);
	}
	for (int c = 0; c < 4; ++c)
	{
		constants.Color[c] = color[c];
	}
	// UvRect 항등 → VS 가 쿼드 로컬 uv[0,1] 를 그대로 전달(Point 감쇠 중심 계산에 사용).
	constants.UvRect[2] = 1.0f;
	constants.UvRect[3] = 1.0f;
	constants.ShaderParams[0] = static_cast<float>(type);
	constants.ShaderParams[1] = intensity;
	return constants;
}

void CForward2DRenderer::DrawLight2D(IRHICommandContext& commandContext, int type, float worldX, float worldY,
	float range, const float color[4], float intensity,
	SafePtr<IRHITexture> shadowAtlas, int shadowRow, int shadowRowCount, float softness01)
{
	if (!m_lightPipeline || !m_quadMesh || !m_defaultSampler || !m_whiteTexture || false == m_rhiDevice.IsValid())
	{
		return;
	}
	const ViewParameters view = BuildViewParameters();
	SpriteConstants constants = BuildLightConstants(type, worldX, worldY, range, color, intensity, view);
	// 그림자면 ShadowAtlas 바인딩 + ShadowParams(행/행수/softness) → PS 가 (θ,r) 폴라 샘플로 light 차폐.
	const bool useShadow = shadowAtlas.IsValid() && shadowRow >= 0;
	SafePtr<IRHITexture> tex = useShadow ? shadowAtlas : m_whiteTexture.GetSafePtr();
	constants.ShadowParams[0] = useShadow ? static_cast<float>(shadowRow) : -1.0f;
	constants.ShadowParams[1] = static_cast<float>(shadowRowCount > 0 ? shadowRowCount : 1);
	constants.ShadowParams[2] = softness01;
	constants.ShadowParams[3] = 0.0f;
	RenderStateCache stateCache;
	DrawSpriteQuad(
		commandContext,
		stateCache,
		m_lightPipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(),
		tex,
		m_defaultSampler.GetSafePtr(),
		constants);
}

void CForward2DRenderer::DrawLight2DSpot(IRHICommandContext& commandContext, float worldX, float worldY,
	float range, const float color[4], float intensity,
	float dirX, float dirY, float innerAngleRadians, float outerAngleRadians,
	SafePtr<IRHITexture> shadowAtlas, int shadowRow, int shadowRowCount, float softness01)
{
	if (!m_lightPipeline || !m_quadMesh || !m_defaultSampler || !m_whiteTexture || false == m_rhiDevice.IsValid())
	{
		return;
	}
	const ViewParameters view = BuildViewParameters();
	// Point 와 동일한 쿼드/반경 감쇠(type=2) 위에 각도 감쇠를 얹는다.
	SpriteConstants constants = BuildLightConstants(2, worldX, worldY, range, color, intensity, view);
	// SecondaryColor 에 스팟 파라미터 패킹: xy=월드 방향, z=cos(inner/2), w=cos(outer/2).
	const float innerHalf = innerAngleRadians * 0.5f;
	const float outerHalf = outerAngleRadians * 0.5f;
	constants.SecondaryColor[0] = dirX;
	constants.SecondaryColor[1] = dirY;
	constants.SecondaryColor[2] = std::cos(innerHalf);
	constants.SecondaryColor[3] = std::cos(outerHalf);
	const bool useShadow = shadowAtlas.IsValid() && shadowRow >= 0;
	SafePtr<IRHITexture> tex = useShadow ? shadowAtlas : m_whiteTexture.GetSafePtr();
	constants.ShadowParams[0] = useShadow ? static_cast<float>(shadowRow) : -1.0f;
	constants.ShadowParams[1] = static_cast<float>(shadowRowCount > 0 ? shadowRowCount : 1);
	constants.ShadowParams[2] = softness01;
	constants.ShadowParams[3] = 0.0f;
	RenderStateCache stateCache;
	DrawSpriteQuad(
		commandContext,
		stateCache,
		m_lightPipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(),
		tex,
		m_defaultSampler.GetSafePtr(),
		constants);
}

bool CForward2DRenderer::AreLightingPipelinesReady() const
{
	return static_cast<bool>(m_lightPipeline) && static_cast<bool>(m_compositePipeline);
}

void CForward2DRenderer::CompositeLighting(IRHICommandContext& commandContext, SafePtr<IRHITexture> sceneColor, SafePtr<IRHITexture> lightMap)
{
	// 1) SceneColor 를 최종 타겟에 불투명 복사.
	BlitFullscreen(commandContext, sceneColor);
	// 2) LightMap 을 곱셈 블렌드로 덮는다 → out.rgb = scene × light, 알파 보존.
	if (!m_compositePipeline || !m_quadMesh || !m_defaultSampler || false == lightMap.IsValid())
	{
		return;
	}
	const SpriteConstants constants = BuildViewportColorConstants(1.0f, 1.0f, 1.0f, 1.0f);
	RenderStateCache stateCache;
	DrawSpriteQuad(
		commandContext,
		stateCache,
		m_compositePipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(),
		lightMap,
		m_defaultSampler.GetSafePtr(),
		constants);
}

void CForward2DRenderer::TonemapFullscreen(IRHICommandContext& commandContext, SafePtr<IRHITexture> src)
{
	// 톤맵 파이프라인 없으면 단순 복사 폴백(클램프될 뿐 화면은 나온다).
	SafePtr<IRHIGraphicsPipeline> pipeline = m_tonemapPipeline ? m_tonemapPipeline.GetSafePtr() : m_blitPipeline.GetSafePtr();
	if (false == pipeline.IsValid() || !m_quadMesh || false == src.IsValid() || !m_defaultSampler)
	{
		return;
	}
	const SpriteConstants constants = BuildViewportColorConstants(1.0f, 1.0f, 1.0f, 1.0f);
	RenderStateCache stateCache;
	DrawSpriteQuad(
		commandContext,
		stateCache,
		pipeline,
		m_quadMesh.GetSafePtr(),
		src,
		m_defaultSampler.GetSafePtr(),
		constants);
}

void CForward2DRenderer::RenderOccluders(IRHICommandContext& commandContext, IRenderScene& scene)
{
	// CastShadow 스프라이트의 알파 실루엣을 현재 바인딩된 OccluderMap 타겟에 Additive 채운다(불투명=1).
	// 월드공간 공유 맵 — 광원별 제외 없음. 뷰는 호출자가 SetViewCameraEx 로 설정(카메라뷰).
	if (false == m_isInitialized || false == m_rhiDevice.IsValid() || !m_occluderFillPipeline || !m_quadMesh)
	{
		return;
	}
	scene.Sort();
	const RenderItem* items = scene.GetRenderItems();
	const std::uint32_t itemCount = scene.GetRenderItemCount();
	const ViewParameters view = BuildViewParameters();
	RenderStateCache stateCache;
	for (std::uint32_t i = 0; i < itemCount; ++i)
	{
		const RenderItem& item = items[i];
		if (false == item.CastShadow)
		{
			continue;
		}
		const SpriteDrawResources resources = ResolveSpriteDrawResources(item);
		if (false == resources.Texture.IsValid() || false == resources.Mesh.IsValid() || false == resources.Sampler.IsValid())
		{
			continue;
		}
		const SpriteConstants constants = BuildSpriteConstants(item, view);
		DrawSpriteQuad(commandContext, stateCache, m_occluderFillPipeline.GetSafePtr(),
			resources.Mesh, resources.Texture, resources.Sampler, constants);
	}
}

bool CForward2DRenderer::CreateOccluderFillPipeline()
{
	// 스프라이트 VS + occluder 알파→흰색 PS + Additive. OccluderMap 에 occluder footprint 채움.
	if (!m_spriteVertexProgram)
	{
		return false;
	}
	const ERHIApi api = m_rhiDevice->GetApi();
	if (ERHIApi::Vulkan == api)
	{
		return true;   // Vulkan SPIRV 미제공 — 스킵(자기 그림자 없이 엣지 그림자만).
	}
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* source = ERHIApi::WebGPU == api ? OCCLUDERFILL_SHADER_SOURCE_WGSL : OCCLUDERFILL_SHADER_SOURCE_HLSL;

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = "PSMain";
	pixelProgramDesc.Source = source;
	m_occluderFillPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);
	if (!m_occluderFillPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	desc.PixelProgram = m_occluderFillPixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Additive;
	m_occluderFillPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_occluderFillPipeline);
}

bool CForward2DRenderer::IsSpriteItemVisibleInView(const RenderItem& item, const ViewParameters& view) const
{
	if (view.HalfW <= 0.0f || view.HalfH <= 0.0f)
	{
		return true;
	}

	const float corners[4][2] = {
		{ -item.LocalHalfExtents[0], -item.LocalHalfExtents[1] },
		{  item.LocalHalfExtents[0], -item.LocalHalfExtents[1] },
		{  item.LocalHalfExtents[0],  item.LocalHalfExtents[1] },
		{ -item.LocalHalfExtents[0],  item.LocalHalfExtents[1] },
	};

	float minX =  FLT_MAX;
	float minY =  FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (const auto& corner : corners)
	{
		const float worldX = item.Transform.M11 * corner[0] + item.Transform.M21 * corner[1] + item.Transform.Dx;
		const float worldY = item.Transform.M12 * corner[0] + item.Transform.M22 * corner[1] + item.Transform.Dy;
		const float dx = worldX - m_viewCamX;
		const float dy = worldY - m_viewCamY;
		const float viewX =  view.CosR * dx + view.SinR * dy;
		const float viewY = -view.SinR * dx + view.CosR * dy;

		minX = std::min(minX, viewX);
		minY = std::min(minY, viewY);
		maxX = std::max(maxX, viewX);
		maxY = std::max(maxY, viewY);
	}

	return !(maxX < -view.HalfW || minX > view.HalfW || maxY < -view.HalfH || minY > view.HalfH);
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteConstantBuffer(IRHICommandContext& commandContext, const SpriteConstants& constants)
{
	const std::size_t bufferIndex = m_spriteConstantBufferCursor++;
	if (bufferIndex >= m_spriteConstantBuffers.size())
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = sizeof(SpriteConstants);
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}
		m_spriteConstantBuffers.push_back(std::move(buffer));
	}

	SafePtr<IRHIBuffer> buffer = m_spriteConstantBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, &constants, sizeof(SpriteConstants));
	return buffer;
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteViewConstantBuffer(IRHICommandContext& commandContext, const SpriteViewConstants& constants)
{
	const std::size_t bufferIndex = m_spriteViewConstantBufferCursor++;
	if (bufferIndex >= m_spriteViewConstantBuffers.size())
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = sizeof(SpriteViewConstants);
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}
		m_spriteViewConstantBuffers.push_back(std::move(buffer));
	}

	SafePtr<IRHIBuffer> buffer = m_spriteViewConstantBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, &constants, sizeof(SpriteViewConstants));
	return buffer;
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteInstanceBuffer(IRHICommandContext& commandContext, const SpriteInstanceData* instances, std::uint32_t instanceCount)
{
	if (nullptr == instances || 0 == instanceCount)
	{
		return nullptr;
	}

	const std::size_t requiredBytes = sizeof(SpriteInstanceData) * static_cast<std::size_t>(instanceCount);
	const std::size_t bufferIndex = m_spriteInstanceBufferCursor++;
	if (bufferIndex >= m_spriteInstanceBuffers.size()
		|| !m_spriteInstanceBuffers[bufferIndex]
		|| m_spriteInstanceBuffers[bufferIndex]->GetDesc().SizeInBytes < requiredBytes)
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = requiredBytes;
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}

		if (bufferIndex >= m_spriteInstanceBuffers.size())
		{
			m_spriteInstanceBuffers.push_back(std::move(buffer));
		}
		else
		{
			m_spriteInstanceBuffers[bufferIndex] = std::move(buffer);
		}
	}

	SafePtr<IRHIBuffer> buffer = m_spriteInstanceBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, instances, requiredBytes);
	return buffer;
}

bool CForward2DRenderer::DrawSpriteItem(
	IRHICommandContext& commandContext,
	RenderStateCache& stateCache,
	const RenderItem& item,
	const SpriteDrawResources& resources,
	const ViewParameters& view)
{
	if (false == resources.Mesh.IsValid() || false == resources.Pipeline.IsValid() || false == resources.Texture.IsValid() || false == resources.Sampler.IsValid())
	{
		return false;
	}

	const SpriteConstants constants = BuildSpriteConstants(item, view);
	return DrawSpriteQuad(commandContext, stateCache, resources.Pipeline, resources.Mesh, resources.Texture, resources.Sampler, constants);
}

bool CForward2DRenderer::DrawSpriteQuad(
	IRHICommandContext& commandContext,
	RenderStateCache& stateCache,
	SafePtr<IRHIGraphicsPipeline> pipeline,
	SafePtr<IRenderMesh> mesh,
	SafePtr<IRHITexture> texture,
	SafePtr<IRHISampler> sampler,
	const SpriteConstants& constants)
{
	if (false == pipeline.IsValid() || false == mesh.IsValid() || false == texture.IsValid() || false == sampler.IsValid())
	{
		return false;
	}

	SafePtr<IRHIBuffer> vertexBuffer = mesh->GetVertexBuffer();
	SafePtr<IRHIBuffer> indexBuffer = mesh->GetIndexBuffer();
	if (false == vertexBuffer.IsValid() || false == indexBuffer.IsValid())
	{
		return false;
	}

	SafePtr<IRHIBuffer> constantBuffer = AcquireSpriteConstantBuffer(commandContext, constants);
	if (false == constantBuffer.IsValid())
	{
		return false;
	}

	if (stateCache.Pipeline != pipeline)
	{
		commandContext.SetGraphicsPipeline(pipeline);
		stateCache.Pipeline = pipeline;
	}

	constexpr std::uint32_t spriteVertexStride = sizeof(SpriteVertex);
	constexpr std::uint32_t spriteVertexOffset = 0;
	if (stateCache.VertexBuffer != vertexBuffer
		|| stateCache.VertexStride != spriteVertexStride
		|| stateCache.VertexOffset != spriteVertexOffset)
	{
		commandContext.SetVertexBuffer(0, vertexBuffer, spriteVertexStride, spriteVertexOffset);
		stateCache.VertexBuffer = vertexBuffer;
		stateCache.VertexStride = spriteVertexStride;
		stateCache.VertexOffset = spriteVertexOffset;
	}
	if (stateCache.IndexBuffer != indexBuffer)
	{
		commandContext.SetIndexBuffer(indexBuffer);
		stateCache.IndexBuffer = indexBuffer;
	}

	commandContext.SetConstantBuffer(ERHIProgramStage::Vertex, 0, constantBuffer);
	commandContext.SetConstantBuffer(ERHIProgramStage::Pixel, 0, constantBuffer);

	if (stateCache.Texture != texture)
	{
		commandContext.SetTexture(ERHIProgramStage::Pixel, 0, texture);
		stateCache.Texture = texture;
	}
	if (stateCache.Sampler != sampler)
	{
		commandContext.SetSampler(ERHIProgramStage::Pixel, 0, sampler);
		stateCache.Sampler = sampler;
	}

	commandContext.DrawIndexed(mesh->GetIndexCount(), 0, 0);
	return true;
}

bool CForward2DRenderer::CanBatchSpriteItem(const RenderItem& item, const SpriteDrawResources& resources) const
{
	if (!m_spriteBatchPipeline || !m_quadMesh)
	{
		return false;
	}
	if (false == item.Material.IsValid()
		|| false == resources.Mesh.IsValid()
		|| false == resources.Texture.IsValid()
		|| false == resources.Sampler.IsValid()
		|| false == resources.Pipeline.IsValid())
	{
		return false;
	}
	if (resources.Mesh != m_quadMesh.GetSafePtr())
	{
		return false;
	}

	return resources.Pipeline == m_spritePipeline.GetSafePtr();
}

bool CForward2DRenderer::DrawSpriteBatch(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem* items, std::uint32_t itemCount, const SpriteDrawResources& resources, const ViewParameters& view)
{
	if (nullptr == items || 0 == itemCount || !m_spriteBatchPipeline || !m_quadMesh)
	{
		return false;
	}

	const RenderItem& firstItem = items[0];
	if (false == firstItem.Material.IsValid())
	{
		return false;
	}

	if (false == CanBatchSpriteItem(firstItem, resources))
	{
		return false;
	}

	SafePtr<IRHIBuffer> vertexBuffer = resources.Mesh->GetVertexBuffer();
	SafePtr<IRHIBuffer> indexBuffer = resources.Mesh->GetIndexBuffer();
	if (false == vertexBuffer.IsValid() || false == indexBuffer.IsValid())
	{
		return false;
	}

	m_spriteBatchInstances.clear();
	m_spriteBatchInstances.reserve(itemCount);
	for (std::uint32_t i = 0; i < itemCount; ++i)
	{
		m_spriteBatchInstances.push_back(BuildSpriteInstanceData(items[i]));
	}

	const SpriteViewConstants viewConstants = BuildSpriteViewConstants(view);
	SafePtr<IRHIBuffer> viewConstantBuffer = AcquireSpriteViewConstantBuffer(commandContext, viewConstants);
	SafePtr<IRHIBuffer> instanceBuffer = AcquireSpriteInstanceBuffer(commandContext, m_spriteBatchInstances.data(), itemCount);
	if (false == viewConstantBuffer.IsValid() || false == instanceBuffer.IsValid())
	{
		return false;
	}

	SafePtr<IRHIGraphicsPipeline> pipeline = m_spriteBatchPipeline.GetSafePtr();
	if (stateCache.Pipeline != pipeline)
	{
		commandContext.SetGraphicsPipeline(pipeline);
		stateCache.Pipeline = pipeline;
	}

	constexpr std::uint32_t spriteVertexStride = sizeof(SpriteVertex);
	constexpr std::uint32_t spriteVertexOffset = 0;
	if (stateCache.VertexBuffer != vertexBuffer
		|| stateCache.VertexStride != spriteVertexStride
		|| stateCache.VertexOffset != spriteVertexOffset)
	{
		commandContext.SetVertexBuffer(0, vertexBuffer, spriteVertexStride, spriteVertexOffset);
		stateCache.VertexBuffer = vertexBuffer;
		stateCache.VertexStride = spriteVertexStride;
		stateCache.VertexOffset = spriteVertexOffset;
	}

	constexpr std::uint32_t spriteInstanceStride = sizeof(SpriteInstanceData);
	constexpr std::uint32_t spriteInstanceOffset = 0;
	if (stateCache.InstanceBuffer != instanceBuffer
		|| stateCache.InstanceStride != spriteInstanceStride
		|| stateCache.InstanceOffset != spriteInstanceOffset)
	{
		commandContext.SetVertexBuffer(1, instanceBuffer, spriteInstanceStride, spriteInstanceOffset);
		stateCache.InstanceBuffer = instanceBuffer;
		stateCache.InstanceStride = spriteInstanceStride;
		stateCache.InstanceOffset = spriteInstanceOffset;
	}

	if (stateCache.IndexBuffer != indexBuffer)
	{
		commandContext.SetIndexBuffer(indexBuffer);
		stateCache.IndexBuffer = indexBuffer;
	}

	commandContext.SetConstantBuffer(ERHIProgramStage::Vertex, 0, viewConstantBuffer);
	commandContext.SetConstantBuffer(ERHIProgramStage::Pixel, 0, viewConstantBuffer);

	if (stateCache.Texture != resources.Texture)
	{
		commandContext.SetTexture(ERHIProgramStage::Pixel, 0, resources.Texture);
		stateCache.Texture = resources.Texture;
	}
	if (stateCache.Sampler != resources.Sampler)
	{
		commandContext.SetSampler(ERHIProgramStage::Pixel, 0, resources.Sampler);
		stateCache.Sampler = resources.Sampler;
	}

	commandContext.DrawIndexedInstanced(resources.Mesh->GetIndexCount(), itemCount, 0, 0, 0);
	return true;
}

template<typename FSkip>
void CForward2DRenderer::RenderWithSkip(IRenderScene& scene, FSkip&& shouldSkip)
{
	m_lastCullingStats = {};

	if (false == m_isInitialized || false == m_rhiDevice.IsValid())
	{
		return;
	}

	if (!m_spritePipeline || !m_quadMesh)
	{
		return;
	}

	scene.Sort();

	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	const RenderItem* items = scene.GetRenderItems();
	const std::uint32_t itemCount = scene.GetRenderItemCount();
	const ViewParameters view = BuildViewParameters();
	RenderStateCache stateCache;
	for (std::uint32_t i = 0; i < itemCount;)
	{
		const RenderItem& item = items[i];

		// 스킵 판정 — RenderImpl=제외 집합 포함, RenderFiltered=필터 집합 미포함.
		if (shouldSkip(item.Entity))
		{
			++i;
			continue;
		}
		++m_lastCullingStats.SubmittedCount;

		if (false == IsSpriteItemVisibleInView(item, view))
		{
			++m_lastCullingStats.CulledCount;
			++i;
			continue;
		}

		const SpriteDrawResources resources = ResolveSpriteDrawResources(item);
		if (CanBatchSpriteItem(item, resources))
		{
			std::uint32_t batchCount = 1;
			while (i + batchCount < itemCount)
			{
				const RenderItem& nextItem = items[i + batchCount];
				if (shouldSkip(nextItem.Entity))
				{
					break;
				}

				if (false == IsSpriteItemVisibleInView(nextItem, view))
				{
					break;
				}

				const SpriteDrawResources nextResources = ResolveSpriteDrawResources(nextItem);
				if (nextResources.Texture != resources.Texture
					|| nextResources.Sampler != resources.Sampler
					|| false == CanBatchSpriteItem(nextItem, nextResources))
				{
					break;
				}
				++batchCount;
			}

			if (batchCount > 1)
			{
				if (DrawSpriteBatch(*commandContext.TryGet(), stateCache, items + i, batchCount, resources, view))
				{
					m_lastCullingStats.SubmittedCount += batchCount - 1;
					m_lastCullingStats.DrawnCount += batchCount;
					i += batchCount;
					continue;
				}
			}
		}

		if (DrawSpriteItem(*commandContext.TryGet(), stateCache, item, resources, view))
		{
			++m_lastCullingStats.DrawnCount;
		}
		++i;
	}
}

void CForward2DRenderer::RenderImpl(IRenderScene& scene, const std::unordered_set<RenderObjectId>* excluded)
{
	// EditorHidden — 제외 집합에 든 오브젝트를 건너뛴다(집합 없으면 아무것도 제외 안 함).
	RenderWithSkip(scene, [excluded](RenderObjectId entity)
	{
		return excluded && excluded->find(entity) != excluded->end();
	});
}

void CForward2DRenderer::RenderFiltered(IRenderScene& scene, const std::unordered_set<RenderObjectId>& objects)
{
	// 빈 필터는 아무것도 그리지 않는다(통계만 리셋). Sort/순회 자체를 건너뛰는 조기 반환.
	if (objects.empty())
	{
		m_lastCullingStats = {};
		return;
	}
	// 필터 집합에 없는 오브젝트를 건너뛴다(포커스/선택 렌더).
	RenderWithSkip(scene, [&objects](RenderObjectId entity)
	{
		return objects.find(entity) == objects.end();
	});
}

void CForward2DRenderer::FillViewportColor(float r, float g, float b, float a)
{
	if (!m_spritePipeline || !m_quadMesh || !m_whiteTexture || !m_rhiDevice.IsValid())
	{
		return;
	}
	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	const SpriteConstants constants = BuildViewportColorConstants(r, g, b, a);
	RenderStateCache stateCache;
	DrawSpriteQuad(
		*commandContext.TryGet(),
		stateCache,
		m_spritePipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(),
		m_whiteTexture.GetSafePtr(),
		m_defaultSampler.GetSafePtr(),
		constants);
}

void CForward2DRenderer::Finalize()
{
	m_whiteTexture.Reset();
	m_weavePool.Clear();
	m_spriteBatchInstances.clear();
	m_spriteInstanceBuffers.clear();
	m_spriteViewConstantBuffers.clear();
	m_spriteConstantBuffers.clear();
	m_spriteInstanceBufferCursor = 0;
	m_spriteViewConstantBufferCursor = 0;
	m_spriteConstantBufferCursor = 0;
	m_quadMesh.Reset();
	m_defaultSampler.Reset();
	m_spriteBatchPipeline.Reset();
	m_tonemapPipeline.Reset();
	m_tonemapPixelProgram.Reset();
	m_compositePipeline.Reset();
	m_lightPipeline.Reset();
	m_lightPixelProgram.Reset();
	m_blitPipeline.Reset();
	m_textPipeline.Reset();
	m_spritePipeline.Reset();
	m_textPixelProgram.Reset();
	m_textVertexProgram.Reset();
	m_spriteBatchPixelProgram.Reset();
	m_spriteBatchVertexProgram.Reset();
	m_spritePixelProgram.Reset();
	m_spriteVertexProgram.Reset();
	m_rhiDevice.Reset();
	m_isInitialized = false;
}

bool CForward2DRenderer::CreateGpuResource(IRenderResource& resource)
{
	(void)resource;
	return true;
}

void CForward2DRenderer::DestroyGpuResource(IRenderResource& resource)
{
	(void)resource;
}

SafePtr<IRHIGraphicsPipeline> CForward2DRenderer::GetSpritePipeline() const
{
	return m_spritePipeline.GetSafePtr();
}

SafePtr<IRHISampler> CForward2DRenderer::GetDefaultSampler() const
{
	return m_defaultSampler.GetSafePtr();
}

SafePtr<IRenderMesh> CForward2DRenderer::GetQuadMesh() const
{
	return m_quadMesh.GetSafePtr();
}

SafePtr<IRHIGraphicsPipeline> CForward2DRenderer::GetTextPipeline() const
{
	return m_textPipeline.GetSafePtr();
}

bool CForward2DRenderer::CreateTextPipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	if (ERHIApi::Vulkan == api)
	{
		return true;
	}
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* source = ERHIApi::WebGPU == api ? TEXT_SHADER_SOURCE_WGSL : TEXT_SHADER_SOURCE_HLSL;
	RHIProgramDesc vertexDesc;
	vertexDesc.Stage = ERHIProgramStage::Vertex;
	vertexDesc.Language = language;
	vertexDesc.EntryPoint = "VSMain";
	vertexDesc.Source = source;
	m_textVertexProgram = m_rhiDevice->CreateProgram(vertexDesc);
	RHIProgramDesc pixelDesc;
	pixelDesc.Stage = ERHIProgramStage::Pixel;
	pixelDesc.Language = language;
	pixelDesc.EntryPoint = "PSMain";
	pixelDesc.Source = source;
	m_textPixelProgram = m_rhiDevice->CreateProgram(pixelDesc);
	if (!m_textVertexProgram || !m_textPixelProgram) return false;
	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;
	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_textVertexProgram.GetSafePtr();
	desc.PixelProgram = m_textPixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::AlphaBlend;
	m_textPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_textPipeline);
}

SafePtr<IRHITexture> CForward2DRenderer::GetWhiteTexture() const
{
	return m_whiteTexture.GetSafePtr();
}

bool CForward2DRenderer::CreateSpritePipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	const ERHIProgramLanguage language =
		ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL :
		ERHIApi::Vulkan == api ? ERHIProgramLanguage::SPIRV :
		ERHIProgramLanguage::HLSL;
	const char* shaderSource =
		ERHIApi::WebGPU == api ? SPRITE_SHADER_SOURCE_WGSL :
		ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpriteVertexSpv) :
		SPRITE_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "VSMain";
	vertexProgramDesc.Source = shaderSource;
	vertexProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpriteVertexSpvSize : 0;
	m_spriteVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "PSMain";
	pixelProgramDesc.Source = ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpritePixelSpv) : shaderSource;
	pixelProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpritePixelSpvSize : 0;
	m_spritePixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);

	if (!m_spriteVertexProgram || !m_spritePixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc pipelineDesc;
	pipelineDesc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	pipelineDesc.PixelProgram = m_spritePixelProgram.GetSafePtr();
	pipelineDesc.VertexElements = elements;
	pipelineDesc.VertexElementCount = 2;
	pipelineDesc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	pipelineDesc.BlendMode = ERHIBlendMode::AlphaBlend; // 스프라이트 투명도 지원
	m_spritePipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_spritePipeline);
}

bool CForward2DRenderer::CreateBlitPipeline()
{
	// 스프라이트 셰이더(POSITION+TEXCOORD, SpriteConstants) 그대로 재사용 —
	// color=white + UvRect 항등이면 out = 텍스처. Opaque 블렌드로 픽셀 정확 복사.
	if (!m_spriteVertexProgram || !m_spritePixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	desc.PixelProgram = m_spritePixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Opaque;   // 순수 복사 — 알파 이중적용 방지
	m_blitPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_blitPipeline);
}

bool CForward2DRenderer::CreateLightPipeline()
{
	// 스프라이트 VS(POSITION+TEXCOORD, SpriteConstants) + 라이트 감쇠 PS + Additive 누적.
	if (!m_spriteVertexProgram)
	{
		return false;
	}
	const ERHIApi api = m_rhiDevice->GetApi();
	if (ERHIApi::Vulkan == api)
	{
		// Vulkan SPIRV 라이트 PS 미제공(비활성 타깃) — 라이팅 파이프라인 스킵.
		return true;
	}
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* source = ERHIApi::WebGPU == api ? LIGHT_SHADER_SOURCE_WGSL : LIGHT_SHADER_SOURCE_HLSL;

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = "PSMain";
	pixelProgramDesc.Source = source;
	m_lightPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);
	if (!m_lightPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	desc.PixelProgram = m_lightPixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Additive;   // 라이트 기여 누적
	m_lightPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_lightPipeline);
}

bool CForward2DRenderer::CreatePolarReductionPipeline()
{
	// 전용 풀스크린 VS(POSITION 직접 NDC) + 레이마치 PS + Opaque. OccluderMap → 각도별 최근접 거리 1D.
	const ERHIApi api = m_rhiDevice->GetApi();
	if (ERHIApi::Vulkan == api)
	{
		return true;   // Vulkan SPIRV 미제공 — 스킵(리덕션 no-op → 무그림자).
	}
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* source = ERHIApi::WebGPU == api ? REDUCTION_SHADER_SOURCE_WGSL : REDUCTION_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = "VSMain";
	vertexProgramDesc.Source = source;
	m_polarReductionVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = "PSMain";
	pixelProgramDesc.Source = source;
	m_polarReductionPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);

	if (!m_polarReductionVertexProgram || !m_polarReductionPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_polarReductionVertexProgram.GetSafePtr();
	desc.PixelProgram = m_polarReductionPixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Opaque;
	m_polarReductionPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_polarReductionPipeline);
}

void CForward2DRenderer::DrawPolarReduction(IRHICommandContext& commandContext, SafePtr<IRHITexture> occluderMap,
	float camX, float camY, float camHalfW, float camHalfH, float camCosR, float camSinR,
	float lightX, float lightY, float lightRange)
{
	if (!m_polarReductionPipeline || !m_quadMesh || !m_defaultSampler || false == occluderMap.IsValid())
	{
		return;
	}
	// OccluderMap 을 그린 카메라 뷰로 world→NDC 행을 만든다(리덕션 PS 가 월드점 → 오클루더 UV 재구성에 사용).
	SetViewCameraEx(camX, camY, camHalfW, camHalfH, camCosR, camSinR);
	const ViewParameters view = BuildViewParameters();
	const SpriteViewConstants viewConstants = BuildSpriteViewConstants(view);
	SpriteConstants constants{};
	for (int i = 0; i < 4; ++i)
	{
		constants.ViewRow0[i] = viewConstants.ViewRow0[i];
		constants.ViewRow1[i] = viewConstants.ViewRow1[i];
	}
	constants.UvRect[2] = 1.0f;
	constants.UvRect[3] = 1.0f;
	constants.SecondaryColor[0] = lightX;
	constants.SecondaryColor[1] = lightY;
	constants.SecondaryColor[2] = lightRange;
	constants.SecondaryColor[3] = 0.0f;
	RenderStateCache stateCache;
	DrawSpriteQuad(commandContext, stateCache, m_polarReductionPipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(), occluderMap, m_defaultSampler.GetSafePtr(), constants);
}

bool CForward2DRenderer::CreateCompositePipeline()
{
	// 스프라이트 VS+PS 재사용(텍스처 × white = 텍스처) + Multiply 블렌드 → 최종 = 라이트맵 × 씬.
	if (!m_spriteVertexProgram || !m_spritePixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	desc.PixelProgram = m_spritePixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Multiply;
	m_compositePipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_compositePipeline);
}

bool CForward2DRenderer::CreateTonemapPipeline()
{
	// 스프라이트 VS + Reinhard 톤맵 PS + Opaque. HDR 컴포짓 결과 → LDR 최종.
	if (!m_spriteVertexProgram)
	{
		return false;
	}
	const ERHIApi api = m_rhiDevice->GetApi();
	if (ERHIApi::Vulkan == api)
	{
		// Vulkan SPIRV 톤맵 PS 미제공 — 스킵. TonemapFullscreen 이 blit 로 폴백.
		return true;
	}
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* source = ERHIApi::WebGPU == api ? TONEMAP_SHADER_SOURCE_WGSL : TONEMAP_SHADER_SOURCE_HLSL;

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = "PSMain";
	pixelProgramDesc.Source = source;
	m_tonemapPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);
	if (!m_tonemapPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc desc;
	desc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	desc.PixelProgram = m_tonemapPixelProgram.GetSafePtr();
	desc.VertexElements = elements;
	desc.VertexElementCount = 2;
	desc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	desc.BlendMode = ERHIBlendMode::Opaque;
	m_tonemapPipeline = m_rhiDevice->CreateGraphicsPipeline(desc);
	return static_cast<bool>(m_tonemapPipeline);
}

bool CForward2DRenderer::CreateSpriteBatchPipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	const ERHIProgramLanguage language =
		ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL :
		ERHIApi::Vulkan == api ? ERHIProgramLanguage::SPIRV :
		ERHIProgramLanguage::HLSL;
	const char* shaderSource =
		ERHIApi::WebGPU == api ? SPRITE_BATCH_SHADER_SOURCE_WGSL :
		ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpriteBatchVertexSpv) :
		SPRITE_BATCH_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "VSMain";
	vertexProgramDesc.Source = shaderSource;
	vertexProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpriteBatchVertexSpvSize : 0;
	m_spriteBatchVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "PSMain";
	pixelProgramDesc.Source = ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpritePixelSpv) : shaderSource;
	pixelProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpritePixelSpvSize : 0;
	m_spriteBatchPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);

	if (!m_spriteBatchVertexProgram || !m_spriteBatchPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[6];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[0].InputSlot = 0;
	elements[0].InputRate = ERHIVertexInputRate::PerVertex;

	elements[1].SemanticName = "TEXCOORD";
	elements[1].SemanticIndex = 0;
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;
	elements[1].InputSlot = 0;
	elements[1].InputRate = ERHIVertexInputRate::PerVertex;

	elements[2].SemanticName = "TEXCOORD";
	elements[2].SemanticIndex = 1;
	elements[2].Format = ERHIVertexFormat::Float4;
	elements[2].Offset = 0;
	elements[2].InputSlot = 1;
	elements[2].InputRate = ERHIVertexInputRate::PerInstance;

	elements[3].SemanticName = "TEXCOORD";
	elements[3].SemanticIndex = 2;
	elements[3].Format = ERHIVertexFormat::Float4;
	elements[3].Offset = sizeof(float) * 4;
	elements[3].InputSlot = 1;
	elements[3].InputRate = ERHIVertexInputRate::PerInstance;

	elements[4].SemanticName = "COLOR";
	elements[4].SemanticIndex = 0;
	elements[4].Format = ERHIVertexFormat::Color4;
	elements[4].Offset = sizeof(float) * 8;
	elements[4].InputSlot = 1;
	elements[4].InputRate = ERHIVertexInputRate::PerInstance;

	// per-instance UvRect(스프라이트시트 프레임). SpriteInstanceData 의 Color 뒤(offset 48).
	// WGSL @location(5) / HLSL TEXCOORD3 와 매칭.
	elements[5].SemanticName = "TEXCOORD";
	elements[5].SemanticIndex = 3;
	elements[5].Format = ERHIVertexFormat::Float4;
	elements[5].Offset = sizeof(float) * 12;
	elements[5].InputSlot = 1;
	elements[5].InputRate = ERHIVertexInputRate::PerInstance;

	RHIGraphicsPipelineDesc pipelineDesc;
	pipelineDesc.VertexProgram = m_spriteBatchVertexProgram.GetSafePtr();
	pipelineDesc.PixelProgram = m_spriteBatchPixelProgram.GetSafePtr();
	pipelineDesc.VertexElements = elements;
	pipelineDesc.VertexElementCount = 6;
	pipelineDesc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	pipelineDesc.BlendMode = ERHIBlendMode::AlphaBlend;
	m_spriteBatchPipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_spriteBatchPipeline);
}

bool CForward2DRenderer::CreateQuadMesh()
{
	const SpriteVertex vertices[] = {
		{ { -0.5f,  0.5f }, { 0.0f, 0.0f } },
		{ {  0.5f,  0.5f }, { 1.0f, 0.0f } },
		{ {  0.5f, -0.5f }, { 1.0f, 1.0f } },
		{ { -0.5f, -0.5f }, { 0.0f, 1.0f } },
	};
	const std::uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

	RHIBufferDesc vertexBufferDesc;
	vertexBufferDesc.SizeInBytes = sizeof(vertices);
	vertexBufferDesc.Usage = ERHIBufferUsage::Immutable;
	vertexBufferDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);

	RHIBufferDesc indexBufferDesc;
	indexBufferDesc.SizeInBytes = sizeof(indices);
	indexBufferDesc.Usage = ERHIBufferUsage::Immutable;
	indexBufferDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer);

	OwnerPtr<IRHIBuffer> vertexBuffer = m_rhiDevice->CreateBuffer(vertexBufferDesc, vertices);
	OwnerPtr<IRHIBuffer> indexBuffer = m_rhiDevice->CreateBuffer(indexBufferDesc, indices);
	if (!vertexBuffer || !indexBuffer)
	{
		return false;
	}

	m_quadMesh = MakeOwnerPtr<CRenderMesh>(std::move(vertexBuffer), std::move(indexBuffer), 4, 6);
	return true;
}
