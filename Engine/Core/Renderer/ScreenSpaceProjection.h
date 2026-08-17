#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  ScreenSpaceProjection — 화면 공간 레이어의 직교 투영 범위 계산(순수 수학).
//
//  화면 공간 레이어(UI/HUD)는 카메라를 안 본다. 그럼 "1 유닛이 몇 픽셀인가"를 누군가
//  정해야 하는데, 그 답은 **최종 뷰포트 픽셀 렉트**에 달려 있다. 그래서 이 계산은
//  저작 시점이 아니라 그리는 시점에만 확정된다 — 렌더 파이프라인(Core)이 소유한다.
//
//  이 헤더는 RHI/렌더러/캔버스 타입을 전혀 의존하지 않는다(표준 헤더만). 게임 스크립트
//  DLL 이 링크하는 Canvas.obj 도 이 헤더를 include 하므로 링크 클로저를 오염시키면 안 된다.
// ─────────────────────────────────────────────────────────────────────────────

// 화면 공간 레이어에서 "1 유닛이 몇 픽셀인가". 표면 종횡비가 기준과 다를 때의 정책이다.
//
// 종횡비 가변은 모바일 전용 문제가 아니다 — Windows 패키지 창이 리사이즈 가능하고
// (RenderSurfaceTypes.h 의 IsResizable 기본 true, 런타임이 덮지 않는다) 데스크톱 패키지엔
// 레터박스가 없다. 그래서 플랫폼 분기가 아니라 전 플랫폼 공용 저작 속성이다.
enum class EScreenScaleMode : std::uint8_t
{
	FixedHeight,     // 기본. 기준 높이 유지 → 가로가 종횡비 따라 늘고 준다.
	FixedWidth,      // 기준 폭 유지 → 세로가 늘고 준다. 세로 게임용.
	Contain,         // 기준 렉트 전체가 항상 보인다(둘 중 넉넉한 쪽). 여백 생김, 잘림 없음.
	ConstantPixel,   // 1 유닛 = 항상 PPU 픽셀. 픽셀아트/픽셀퍼펙트 UI 용.
};

// 화면 공간 저작 기준. 프로젝트 해상도와 PPU 에서 유도한다(1920x1080 @ 100 → 9.6 x 5.4).
struct ScreenSpaceReference
{
	float HalfWidth     = 9.6f;    // 기준 렉트 반폭(유닛)
	float HalfHeight    = 5.4f;    // 기준 렉트 반높이(유닛)
	float PixelsPerUnit = 100.0f;

	static ScreenSpaceReference FromResolution(float resolutionWidth, float resolutionHeight, float pixelsPerUnit)
	{
		ScreenSpaceReference reference;
		reference.PixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 100.0f;
		reference.HalfWidth  = std::max(resolutionWidth,  1.0f) / reference.PixelsPerUnit * 0.5f;
		reference.HalfHeight = std::max(resolutionHeight, 1.0f) / reference.PixelsPerUnit * 0.5f;
		return reference;
	}
};

// 스케일 모드가 정하는 화면 공간 반높이/반폭(유닛). rectPixel* 은 이 뷰포트 렉트의 픽셀 크기다
// (화면 전체가 아니라 — 스플릿에서 화면 전체 비율을 쓰면 내용이 찌그러진다).
inline void ComputeScreenSpaceExtents(
	EScreenScaleMode mode,
	const ScreenSpaceReference& reference,
	float rectPixelW,
	float rectPixelH,
	float& outHalfWidth,
	float& outHalfHeight)
{
	const float safeRectH = std::max(rectPixelH, 1.0f);
	const float aspect    = std::max(rectPixelW, 1.0f) / safeRectH;

	float halfHeight = reference.HalfHeight;
	switch (mode)
	{
	case EScreenScaleMode::FixedWidth:
		// 기준 폭 유지 → 반높이는 종횡비가 정한다.
		halfHeight = reference.HalfWidth / std::max(aspect, 0.0001f);
		break;
	case EScreenScaleMode::Contain:
		// 기준 렉트가 통째로 들어와야 한다: 반높이 ≥ 기준반높이 **그리고** 반폭 ≥ 기준반폭.
		// 반폭 = 반높이 x aspect 이므로 두 요구를 반높이 하나로 합치면 max 다.
		halfHeight = std::max(reference.HalfHeight, reference.HalfWidth / std::max(aspect, 0.0001f));
		break;
	case EScreenScaleMode::ConstantPixel:
		// 1 유닛 = 항상 PPU 픽셀.
		halfHeight = safeRectH / reference.PixelsPerUnit * 0.5f;
		break;
	case EScreenScaleMode::FixedHeight:
	default:
		halfHeight = reference.HalfHeight;
		break;
	}

	outHalfHeight = std::max(halfHeight, 0.0001f);
	outHalfWidth  = outHalfHeight * aspect;
}

// 직렬화/에디터 공용 — enum ↔ 문자열.
inline const char* ToString(EScreenScaleMode mode)
{
	switch (mode)
	{
	case EScreenScaleMode::FixedWidth:    return "FixedWidth";
	case EScreenScaleMode::Contain:       return "Contain";
	case EScreenScaleMode::ConstantPixel: return "ConstantPixel";
	case EScreenScaleMode::FixedHeight:
	default:                              return "FixedHeight";
	}
}

inline EScreenScaleMode ScreenScaleModeFromString(const char* text)
{
	if (nullptr == text)
	{
		return EScreenScaleMode::FixedHeight;
	}
	if (0 == std::strcmp(text, "FixedWidth"))
	{
		return EScreenScaleMode::FixedWidth;
	}
	if (0 == std::strcmp(text, "Contain"))
	{
		return EScreenScaleMode::Contain;
	}
	if (0 == std::strcmp(text, "ConstantPixel"))
	{
		return EScreenScaleMode::ConstantPixel;
	}
	return EScreenScaleMode::FixedHeight;
}
