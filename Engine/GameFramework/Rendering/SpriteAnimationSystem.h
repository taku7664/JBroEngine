#pragma once

#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

class IAssetManager;

// ─────────────────────────────────────────────────────────────────────────────
//  CSpriteAnimationSystem — SpriteAnimator2D 를 구동해 SpriteRenderer2D.FrameIndex 를
//  시간에 따라 갱신한다.
//
//  · 총 프레임 수는 SpriteRenderer2D.SpriteGuid 자산의 시트 슬라이스에서 조회한다
//    (AssetManager 의존). 프레임 수가 1 이하면 애니메이션이 없는 것으로 간주.
//  · SpriteRenderSystem 보다 먼저 등록해야 같은 프레임에 FrameIndex 가 반영된다.
//  · 편집 모드에서는 동작하지 않는다(ShouldUpdateInEditMode=false) — 정지 프레임은
//    SpriteRenderer2D.FrameIndex 를 직접 편집해 미리볼 수 있다.
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
	SafePtr<IAssetManager> m_assetManager;
};
