#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/SafePtr.h"

#include <unordered_map>
#include <unordered_set>

class IAudioDevice;
class IAudioPlayer;
class IAssetManager;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAudioSystem
//
//  씬 단위 오디오 시스템. AudioListener / AudioPlayer 컴포넌트를 매 프레임 갱신.
//    1. listener: 활성 청취자의 월드 위치를 IAudioDevice::GetPrimaryListener() 로 push.
//    2. player:
//       - 인스턴스가 없으면 자산 path 로 생성 후 PlayOnStart 옵션에 따라 시작.
//       - 인스턴스가 있으면 Volume/Pitch/Loop/Is3D/위치 매 프레임 동기.
//       - 컴포넌트가 disable → 인스턴스 즉시 Stop + 해제 (자원 누수 방지).
//       - non-loop 자산이 IsEnded → 인스턴스 해제.
//
//  편집 모드(에디터 정지 상태) 에서는 동작하지 않는다 — ShouldUpdateInEditMode=false.
//  (편집 중에는 Inspector 의 EditorAudioPreview 가 별도 디바이스로 미리듣기.)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class CAudioSystem final : public CGameSystem
{
public:
	CAudioSystem(SafePtr<IAudioDevice> device   = nullptr,
	             SafePtr<IAssetManager> assetMgr = nullptr);

	void SetDevice      (SafePtr<IAudioDevice> device);
	void SetAssetManager(SafePtr<IAssetManager> assetMgr);

	bool ShouldUpdateInEditMode() const override { return false; }

protected:
	void OnUpdate  (CScene& scene) override;
	void OnFinalize(CScene& scene) override;

private:
	struct PlayerInstance
	{
		OwnerPtr<IAudioPlayer> Player;
		AssetGuid              SourceGuid;   // 자산이 바뀌면 인스턴스를 재생성하기 위해.
	};

	SafePtr<IAudioDevice>  m_device;
	SafePtr<IAssetManager> m_assetManager;
	std::unordered_map<EntityId, PlayerInstance> m_instances;
};
