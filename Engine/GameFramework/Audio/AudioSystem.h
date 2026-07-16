#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/File/FilePath.h"   // File::Guid (인스턴스 키)
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class IAudioDevice;
class IAudioPlayer;
class IAudioEffect;
class IAssetManager;
class AudioPlayer;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAudioSystem
//
//  씬 단위 오디오 시스템. AudioListener / AudioPlayer 컴포넌트를 매 프레임 갱신.
//    1. listener: 활성 청취자의 월드 위치를 IAudioDevice::GetPrimaryListener() 로 push.
//    2. player:
//       - 활성 구간 진입 시 자산 path 로 생성 후 PlayOnStart 옵션에 따라 한 번 시작.
//       - 인스턴스가 있으면 Volume/Pitch/Loop/Is3D/위치 매 프레임 동기.
//       - 컴포넌트가 disable → backend 자원 즉시 해제, 다음 enable에서 재무장.
//       - non-loop 자연 종료와 로딩 실패는 상태로 보존해 매 프레임 재생성을 막음.
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
	void OnUpdate  (CGameScene& scene) override;
	void OnFinalize(CGameScene& scene) override;
	void OnSimulationStop(CGameScene& scene) override;

private:
	struct PlayerInstance
	{
		enum class EState : std::uint8_t
		{
			Inactive,
			PendingCreate,
			Ready,
			Finished,
			LoadFailed,
		};

		PlayerInstance() = default;
		~PlayerInstance();
		PlayerInstance(const PlayerInstance&) = delete;
		PlayerInstance& operator=(const PlayerInstance&) = delete;
		PlayerInstance(PlayerInstance&& other) noexcept;
		PlayerInstance& operator=(PlayerInstance&& other) noexcept;

		// 효과를 먼저 분리한 뒤 Player를 파괴하고 마지막에 Effect owner를 파괴한다.
		// map erase, 시스템 종료, AudioGuid 변경이 모두 이 경로를 사용해야 한다.
		void ResetBackendResources();

		OwnerPtr<IAudioPlayer>              Player;
		AssetGuid                           SourceGuid;        // 자산이 바뀌면 인스턴스를 재생성하기 위해.
		// 효과 체인 — 컴포넌트 EffectGuids 와 동기. 셋은 항상 같은 길이/순서를 유지한다.
		std::vector<AssetGuid>              EffectGuids;       // 리스트 변경(추가/삭제/재정렬) 감지용.
		std::vector<OwnerPtr<IAudioEffect>> Effects;           // 부착된 효과 노드 — player 보다 오래 살아야.
		std::vector<std::uint32_t>          EffectGenerations; // 효과 에셋 값 변경(.jfx) 감지용.
		EState                              State = EState::Inactive;
		bool                                WasEffectivelyEnabled = false;
		bool                                PlayOnStartConsumed = false;
	};

	// 효과 체인 동기 — 리스트 변경(추가/삭제/재정렬)이면 전체 재구성+재배선,
	// 같은 리스트에서 효과 .jfx 값만 바뀌었으면(generation) 해당 노드에 SetParameter 재적용.
	void SyncEffectChain(PlayerInstance& instance, const AudioPlayer& player);

	SafePtr<IAudioDevice>  m_device;
	SafePtr<IAssetManager> m_assetManager;
	// 키 = AudioPlayer 컴포넌트의 InstanceGuid.
	// ⚠ 컴포넌트 raw 주소를 키로 쓰면 안 된다 — 파괴 후 풀 슬롯이 재사용되면 새 컴포넌트가
	//    같은 주소를 받아 죽은 인스턴스(재생 상태/보이스)를 물려받는다. guid 는 재발급되므로 안전.
	std::unordered_map<File::Guid, PlayerInstance> m_instances;
};
