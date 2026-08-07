#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>

class CAnimationClipAsset;
class CGameObject;
class IAssetManager;
class SpriteAnimator2D;
class SpriteRenderer2D;

// ─────────────────────────────────────────────────────────────────────────────
//  CSpriteAnimationSystem — SpriteAnimator2D 를 구동해 SpriteRenderer2D.FrameIndex 를
//  시간에 따라 갱신한다.
//
//  · 총 프레임 수는 시트 자산의 슬라이스에서 조회한다(AssetManager 의존).
//    프레임 수가 1 이하면 애니메이션이 없는 것으로 간주.
//  · SpriteRenderSystem 보다 먼저 등록해야 같은 프레임에 FrameIndex 가 반영된다.
//  · 편집 모드에서는 동작하지 않는다(ShouldUpdateInEditMode=false) — 정지 프레임은
//    SpriteRenderer2D.FrameIndex 를 직접 편집해 미리볼 수 있다.
//
//  클립이 있으면 클립 모드로, 없으면 애니메이터 자체 필드로 재생한다
//  (SpriteAnimator2D.h 의 "두 가지 동작 모드" 참조).
// ─────────────────────────────────────────────────────────────────────────────
class CSpriteAnimationSystem final : public CGameSystem
{
public:
	explicit CSpriteAnimationSystem(SafePtr<IAssetManager> assetManager = nullptr);

	void SetAssetManager(SafePtr<IAssetManager> assetManager);
	bool ShouldUpdateInEditMode() const override { return false; }

protected:
	void OnUpdate(CGameCanvas& canvas) override;
	void OnSimulationStop(CGameCanvas& canvas) override;

private:
	// 시트의 총 프레임 수(슬라이스 개수). 자산이 없거나 스프라이트가 아니면 0.
	std::uint32_t GetSheetFrameCount(const AssetGuid& spriteGuid) const;
	// 애니메이터의 ClipGuids 에서 이름이 맞는 클립의 **인덱스**를 찾는다. 없으면 -1.
	// clipName 이 비어 있으면 첫 번째 유효 클립. 이름 비교가 일어나는 유일한 지점이며,
	// Play 요청이 들어온 틱에만 호출된다.
	std::int32_t FindClipIndex(const SpriteAnimator2D& animator, const char* clipName) const;
	// 인덱스로 클립 자산을 얻는다(자산 캐시 히트 경로).
	CAnimationClipAsset* LoadClipAt(const SpriteAnimator2D& animator, std::int32_t index) const;

	// 클립 모드 / 단일 시트 모드 각각의 한 틱.
	void UpdateClipMode(SpriteAnimator2D& animator, SpriteRenderer2D& sprite, CGameObject& owner, float deltaSeconds);
	void UpdateSheetMode(SpriteAnimator2D& animator, SpriteRenderer2D& sprite, float deltaSeconds);
	// 클립을 처음부터 재생 상태로 만든다(시트 교체 + 상태 리셋 + 0번 프레임 이벤트 발화).
	void StartClip(SpriteAnimator2D& animator, SpriteRenderer2D& sprite, CGameObject& owner,
	               std::int32_t clipIndex, const CAnimationClipAsset& clip);

	// 부착된 스크립트들에 애니메이션 훅을 전달한다(시작된 인스턴스에만).
	static void DispatchEvent(CGameObject* owner, const char* clipName, const char* eventName);
	static void DispatchEnd(CGameObject* owner, const char* clipName);

	SafePtr<IAssetManager> m_assetManager;
};
