#pragma once

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Ease — 정규화 진행도 t(0..1) 를 다시 0..1 로 매핑하는 완급 곡선.
//
//  전부 `float(*)(float)` 로 통일한다 — 함수 포인터는 POD 라 호스트↔게임 DLL 경계를
//  넘어도 안전하고, 트윈이 이걸 코루틴 프레임에 값으로 담을 수 있다(힙 할당 없음).
//
//  계약: Ease(0) == 0, Ease(1) == 1. Back/Elastic/Bounce 는 중간에 0..1 을 **벗어난다**
//  (그게 그 곡선의 목적이다) — 색이나 알파처럼 범위를 벗어나면 안 되는 값에는 쓰지 말 것.
//
//  이름 규칙은 통용되는 것을 따른다: In=시작이 느림, Out=끝이 느림, InOut=양쪽이 느림.
//  UI 등장/카메라 이동에는 보통 OutCubic 이 무난하다.
// ─────────────────────────────────────────────────────────────────────────────
namespace Ease
{
	using Func = float (*)(float);

	inline float Linear(float t) { return t; }

	inline float InQuad(float t)  { return t * t; }
	inline float OutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
	inline float InOutQuad(float t)
	{
		if (t < 0.5f)
		{
			return 2.0f * t * t;
		}
		const float u = -2.0f * t + 2.0f;
		return 1.0f - (u * u) * 0.5f;
	}

	inline float InCubic(float t)  { return t * t * t; }
	inline float OutCubic(float t)
	{
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
	inline float InOutCubic(float t)
	{
		if (t < 0.5f)
		{
			return 4.0f * t * t * t;
		}
		const float u = -2.0f * t + 2.0f;
		return 1.0f - (u * u * u) * 0.5f;
	}

	inline float InSine(float t)  { return 1.0f - std::cos((t * 3.14159265358979323846f) * 0.5f); }
	inline float OutSine(float t) { return std::sin((t * 3.14159265358979323846f) * 0.5f); }

	// 목표를 살짝 지나쳤다가 돌아온다. 버튼 팝업 같은 데 쓴다.
	inline float OutBack(float t)
	{
		constexpr float overshoot = 1.70158f;
		const float u = t - 1.0f;
		return 1.0f + (overshoot + 1.0f) * u * u * u + overshoot * u * u;
	}

	// 목표 근처에서 진동하며 잦아든다.
	inline float OutElastic(float t)
	{
		if (t <= 0.0f) { return 0.0f; }
		if (t >= 1.0f) { return 1.0f; }
		constexpr float period = (2.0f * 3.14159265358979323846f) / 3.0f;
		return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * period) + 1.0f;
	}

	// 바닥에 튀는 느낌. 낙하 연출에 쓴다.
	inline float OutBounce(float t)
	{
		constexpr float n1 = 7.5625f;
		constexpr float d1 = 2.75f;
		if (t < 1.0f / d1)
		{
			return n1 * t * t;
		}
		if (t < 2.0f / d1)
		{
			const float u = t - 1.5f / d1;
			return n1 * u * u + 0.75f;
		}
		if (t < 2.5f / d1)
		{
			const float u = t - 2.25f / d1;
			return n1 * u * u + 0.9375f;
		}
		const float u = t - 2.625f / d1;
		return n1 * u * u + 0.984375f;
	}
}
