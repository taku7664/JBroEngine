#pragma once

#include "GameFramework/Component/Component.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  SpriteAnimator2D — 스프라이트시트 플립북 애니메이션.
//
//  · 같은 오브젝트의 SpriteRenderer2D.FrameIndex 를 시간에 따라 갱신한다.
//    자산 1개(시트) = 애니메이션 1개. 프레임은 자산의 SliceType 슬라이싱을 따른다.
//  · CSpriteAnimationSystem 이 구동한다. SpriteRenderSystem 보다 먼저 등록되어
//    같은 프레임에 FrameIndex 가 반영된다.
//  · 편집 모드에서는 동작하지 않는다(시스템 ShouldUpdateInEditMode=false).
// ─────────────────────────────────────────────────────────────────────────────
class SpriteAnimator2D final : public CComponent
{
	JBRO_COMPONENT(SpriteAnimator2D)
public:
	// 초당 프레임 진행 수(재생 속도).
	float FramesPerSecond = 12.0f;
	// 끝 프레임 이후 처음으로 순환. false 면 마지막 프레임에서 멈추고 Playing=false.
	bool  Loop = true;
	// 재생 상태(직렬화 — 캔버스 시작 시 재생 여부). non-loop 종료 시 false 로 전환된다.
	bool  Playing = true;
	// 시트에서 재생을 시작할 프레임 인덱스.
	std::uint32_t StartFrame = 0;
	// 재생할 프레임 수. 0 = StartFrame 부터 자산의 마지막 프레임까지.
	std::uint32_t FrameCount = 0;

	// ── 런타임 상태(리플렉션/직렬화 제외) ────────────────────────────────────
	// 현재 프레임 누적 시간(초). frameDuration 을 넘으면 다음 프레임으로.
	float         RuntimeElapsedSeconds = 0.0f;
	// StartFrame 기준 상대 프레임(0 .. range-1).
	std::uint32_t RuntimeLocalFrame     = 0;
};
