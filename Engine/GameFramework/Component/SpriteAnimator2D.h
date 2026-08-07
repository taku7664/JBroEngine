#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Component/Component.h"
#include "Utillity/Types/Array.h"
#include "Utillity/Types/String.h"

#include <cstddef>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  SpriteAnimator2D — 스프라이트시트 플립북 애니메이션.
//
//  · 같은 오브젝트의 SpriteRenderer2D.FrameIndex 를 시간에 따라 갱신한다.
//    프레임은 시트 자산의 SliceType 슬라이싱을 따른다.
//  · CSpriteAnimationSystem 이 구동한다. SpriteRenderSystem 보다 먼저 등록되어
//    같은 프레임에 FrameIndex 가 반영된다.
//  · 편집 모드에서는 동작하지 않는다(시스템 ShouldUpdateInEditMode=false).
//
//  ── 두 가지 동작 모드 ──────────────────────────────────────────────────────
//  1) **클립 모드** — ClipGuids 에 클립이 하나라도 있으면 이 모드다. 이름으로 골라
//     재생하고(`Play("run")`), 클립의 프레임 이벤트를 스크립트로 알린다.
//  2) **단일 시트 모드(폴백)** — 클립이 하나도 없을 때. 아래 FramesPerSecond/Loop/
//     Playing/StartFrame/FrameCount 로 시트를 한 덩어리로 재생한다. 클립 도입 이전의
//     동작이며, 기존 씬이 조용히 멈추지 않도록 남겨 둔다.
// ─────────────────────────────────────────────────────────────────────────────
class SpriteAnimator2D final : public CComponent
{
	JBRO_COMPONENT(SpriteAnimator2D)
public:
	// 이 애니메이터가 재생할 수 있는 클립들(.janimclip). 비어 있으면 단일 시트 모드.
	Array<AssetGuid> ClipGuids;
	// 시작 시 재생할 클립 이름. 비어 있으면 목록의 첫 클립.
	// **저작 필드다** — 호스트(인스펙터/직렬화)가 쓴다. 스크립트는 읽기만 할 것.
	String DefaultClip;

	// ── 단일 시트 모드 파라미터(클립이 없을 때만 쓰인다) ─────────────────────
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
	// 재생 구간 기준 상대 프레임(0 .. range-1).
	std::uint32_t RuntimeLocalFrame     = 0;
	// 지금 재생 중인 클립의 ClipGuids 인덱스. -1 = 아직 정해지지 않음.
	// **이름 비교를 Play 요청 틱으로 한정하려고** 둔 캐시다 — 매 프레임 클립 이름을
	// 문자열 비교하면 프레임 루프에 문자열 연산이 들어간다.
	std::int32_t  RuntimeClipIndex      = -1;

	// ── 스크립트 ↔ 시스템 요청 슬롯 ──────────────────────────────────────────
	// **고정 크기 char 버퍼다.** 게임 DLL 안의 스크립트가 직접 쓰고 호스트가 읽는다.
	// `String`(std::string 파생)을 쓰면 DLL 이 할당한 메모리를 호스트가 해제하게 되므로
	// 이 슬롯들은 POD 여야 한다.
	//
	// Play()/Stop() 은 요청만 남기고 실제 전환은 CSpriteAnimationSystem 이 다음 틱에 한다 —
	// 클립 이름을 풀려면 자산을 로드해야 해서 DLL 쪽에서 끝낼 수 있는 일이 아니다.
	static constexpr std::size_t CLIP_NAME_CAPACITY = 64;
	char RequestedClip[CLIP_NAME_CAPACITY] = {};
	bool HasPlayRequest = false;
	bool HasStopRequest = false;
	// 시스템이 채우는 "지금 재생 중인 클립" 이름. 클립 모드가 아니면 빈 문자열.
	char CurrentClip[CLIP_NAME_CAPACITY] = {};

public:
	// ── 스크립트 API ─────────────────────────────────────────────────────────
	// 이름이 목록에 없으면 무시된다(현재 재생이 그대로 유지된다).
	// 이미 그 클립을 재생 중이어도 처음부터 다시 시작한다.
	void Play(const char* clipName)
	{
		CopyClipName(RequestedClip, clipName);
		HasPlayRequest = true;
		HasStopRequest = false;
	}

	// 현재 프레임에서 멈춘다. Play 로 다시 시작할 수 있다.
	void Stop()
	{
		HasStopRequest = true;
		HasPlayRequest = false;
	}

	// 지금 재생 중인 클립 이름. 클립 모드가 아니거나 아직 정해지지 않았으면 빈 문자열.
	const char* GetCurrentClip() const { return CurrentClip; }

	bool IsPlaying() const { return Playing; }

private:
	static void CopyClipName(char (&destination)[CLIP_NAME_CAPACITY], const char* source)
	{
		if (nullptr == source)
		{
			destination[0] = '\0';
			return;
		}
		std::size_t i = 0;
		for (; i + 1 < CLIP_NAME_CAPACITY && '\0' != source[i]; ++i)
		{
			destination[i] = source[i];
		}
		destination[i] = '\0';
	}
};
