#include "pch.h"
#include "AudioSystem.h"

#include "Core/Asset/AudioAsset.h"
#include "Core/Asset/AudioEffectAsset.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/IAssetRegistry.h"
#include "Core/Audio/AudioTypes.h"
#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/IAudioEffect.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioPlayer.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"

namespace
{
	AudioVec3 ExtractWorldPosition(const CGameObject& object)
	{
		const Matrix3x2& m = object.GetWorld().Matrix;
		AudioVec3 v;
		v.X = m.Dx;   // translation x
		v.Y = m.Dy;   // translation y
		v.Z = 0.0f;
		return v;
	}

	// effectGuid 의 효과 에셋을 찾는다. 효과 에셋이 아니거나 없으면 null.
	CAudioEffectAsset* ResolveEffectAsset(IAssetManager& am, const AssetGuid& effectGuid)
	{
		AssetMetaData meta;
		if (false == am.GetRegistry().TryGetAsset(effectGuid, meta) || EAssetType::AudioEffect != meta.Type) return nullptr;

		AssetRef<IAsset> asset = am.LoadAsset(effectGuid);
		if (false == asset.IsValid() || EAssetType::AudioEffect != asset->GetAssetType()) return nullptr;
		return static_cast<CAudioEffectAsset*>(asset.Get());
	}

	// 효과 에셋의 현재 generation. 없으면 0.
	std::uint32_t EffectGeneration(IAssetManager& am, const AssetGuid& effectGuid)
	{
		CAudioEffectAsset* effectAsset = ResolveEffectAsset(am, effectGuid);
		return effectAsset ? effectAsset->GetGeneration() : 0u;
	}

	// effectGuid 로 효과 노드 하나를 만들고 파라미터를 적용해 반환한다(부착은 호출자가 체인으로).
	// 실패(에셋 없음/효과 생성 실패) 시 null.
	OwnerPtr<IAudioEffect> BuildEffect(IAudioDevice& device, IAssetManager& am, const AssetGuid& effectGuid)
	{
		CAudioEffectAsset* effectAsset = ResolveEffectAsset(am, effectGuid);
		if (nullptr == effectAsset) return nullptr;

		OwnerPtr<IAudioEffect> effect = device.CreateEffect(effectAsset->GetKind());
		if (false == bool(effect)) return nullptr;

		for (const auto& kv : effectAsset->GetParameters())
		{
			effect->SetParameter(kv.first.c_str(), kv.second);
		}
		return effect;
	}
}

CAudioSystem::CAudioSystem(SafePtr<IAudioDevice> device, SafePtr<IAssetManager> assetMgr)
	: m_device(device), m_assetManager(assetMgr)
{
}

CAudioSystem::PlayerInstance::~PlayerInstance()
{
	ResetBackendResources();
}

CAudioSystem::PlayerInstance::PlayerInstance(PlayerInstance&& other) noexcept
{
	*this = std::move(other);
}

CAudioSystem::PlayerInstance& CAudioSystem::PlayerInstance::operator=(PlayerInstance&& other) noexcept
{
	if (this == &other)
	{
		return *this;
	}

	ResetBackendResources();
	Player = std::move(other.Player);
	SourceGuid = std::move(other.SourceGuid);
	Bus = other.Bus;
	EffectGuids = std::move(other.EffectGuids);
	Effects = std::move(other.Effects);
	EffectGenerations = std::move(other.EffectGenerations);
	State = other.State;
	WasEffectivelyEnabled = other.WasEffectivelyEnabled;
	PlayOnStartConsumed = other.PlayOnStartConsumed;
	LastSeenFrame = other.LastSeenFrame;

	other.State = EState::Inactive;
	other.WasEffectivelyEnabled = false;
	other.PlayOnStartConsumed = false;
	return *this;
}

void CAudioSystem::PlayerInstance::ResetBackendResources()
{
	if (Player)
	{
		Player->DetachAllEffects();
		Player->Stop();
		Player.Reset();
	}

	Effects.clear();
	EffectGenerations.clear();
	EffectGuids.clear();
}

void CAudioSystem::SetDevice(SafePtr<IAudioDevice> device)
{
	m_device = device;
}

void CAudioSystem::SetAssetManager(SafePtr<IAssetManager> assetMgr)
{
	m_assetManager = assetMgr;
}

void CAudioSystem::SyncEffectChain(PlayerInstance& instance, const AudioPlayer& player)
{
	if (!instance.Player || false == m_device.IsValid() || false == m_assetManager.IsValid())
	{
		return;
	}
	IAudioDevice&  device = *m_device.TryGet();
	IAssetManager& am     = *m_assetManager.TryGet();

	// 리스트(추가/삭제/재정렬)가 그대로면 — 효과 .jfx 값만 바뀌었는지 generation 으로 확인.
	const bool sameEffectGuids = instance.EffectGuids.size() == player.EffectGuids.Size()
		&& std::equal(instance.EffectGuids.begin(), instance.EffectGuids.end(), player.EffectGuids.begin());
	if (sameEffectGuids)
	{
		for (std::size_t i = 0; i < instance.EffectGuids.size(); ++i)
		{
			const std::uint32_t gen = EffectGeneration(am, instance.EffectGuids[i]);
			if (gen == instance.EffectGenerations[i]) continue;

			// 값이 바뀐 효과 — 노드를 재생성하지 않고 파라미터만 재적용(재생 끊김 없음).
			CAudioEffectAsset* effectAsset = ResolveEffectAsset(am, instance.EffectGuids[i]);
			if (effectAsset && i < instance.Effects.size() && instance.Effects[i])
			{
				for (const auto& kv : effectAsset->GetParameters())
				{
					instance.Effects[i]->SetParameter(kv.first.c_str(), kv.second);
				}
			}
			instance.EffectGenerations[i] = gen;
		}
		return;
	}

	// 리스트가 바뀜 — 체인을 전체 재구성한다.
	// 세 캐시 배열(Effects/EffectGenerations/EffectGuids)은 항상 player.EffectGuids 와 같은
	// 길이·순서를 유지한다. 빌드 실패한 효과는 자리를 null 로 채워(인덱스 정합) chain 에서만 빠진다
	// (SetEffectChain 이 null 노드를 건너뛴다).
	std::vector<OwnerPtr<IAudioEffect>> effects;
	std::vector<std::uint32_t>          generations;
	std::vector<SafePtr<IAudioEffect>>  chain;
	effects.reserve(player.EffectGuids.Size());
	generations.reserve(player.EffectGuids.Size());
	chain.reserve(player.EffectGuids.Size());

	for (const AssetGuid& effectGuid : player.EffectGuids)
	{
		OwnerPtr<IAudioEffect> effect = BuildEffect(device, am, effectGuid);
		if (bool(effect))
		{
			chain.push_back(effect.GetSafePtr());
		}
		generations.push_back(EffectGeneration(am, effectGuid));
		effects.push_back(std::move(effect));   // 실패면 null — 인덱스 정합 유지.
	}

	instance.Player->SetEffectChain(chain);
	instance.Effects           = std::move(effects);
	instance.EffectGenerations = std::move(generations);
	instance.EffectGuids.assign(player.EffectGuids.begin(), player.EffectGuids.end());
}

void CAudioSystem::OnUpdate(CGameCanvas& canvas)
{
	if (false == m_device.IsValid())
	{
		return;
	}

	// ── 1) Listener — 첫 번째 활성 청취자만 사용 ────────────────────────
	SafePtr<IAudioListener> primary = m_device->GetPrimaryListener();
	bool listenerSet = false;
	canvas.ForEach<AudioListener>(
		[&](AudioListener& listener)
		{
			if (listenerSet) return;
			CGameObject* owner = listener.GetOwner().TryGet();
			// TryGet 은 null 을 줄 수 있다(아래 AudioPlayer 루프는 막고 있었는데 여기만 빠져 있었다).
			// SetPosition 이 빈 함수이던 동안에는 드러나지 않던 역참조다.
			if (nullptr == owner) return;
			if (false == IsActiveComponent(listener)) return;
			if (primary.IsValid())
			{
				primary->SetPosition(ExtractWorldPosition(*owner));
				primary->SetMasterVolume(listener.MasterVolume);
			}
			listenerSet = true;
		});

	// ── 2) Player — 컴포넌트별 인스턴스 생성/동기/해제 ─────────────────
	++m_frameStamp;

	canvas.ForEach<AudioPlayer>(
		[&](AudioPlayer& player)
		{
			CGameObject* owner = player.GetOwner().TryGet();
			if (nullptr == owner) return;

			const File::Guid& key = player.GetInstanceGuid(); // 슬롯 재사용 안전(주소 아님).

			const bool effectivelyEnabled = IsActiveComponent(player)
				&& false == player.AudioGuid.IsNull();

			auto [it, inserted] = m_instances.try_emplace(key);
			PlayerInstance& instance = it->second;
			instance.LastSeenFrame = m_frameStamp;
			// 버스도 자산과 같이 취급한다 — 둘 다 backend 인스턴스를 만들 때 확정되는
			// 값이라 살아 있는 player 에 나중에 반영할 수 없다(재생 위치는 초기화된다).
			const bool sourceChanged = inserted
				|| instance.SourceGuid != player.AudioGuid
				|| false == IsSameAudioBusName(instance.Bus.c_str(), player.Bus.c_str());
			if (sourceChanged)
			{
				instance.ResetBackendResources();
				instance.SourceGuid = player.AudioGuid;
				instance.Bus = player.Bus;
				instance.State = effectivelyEnabled
					? PlayerInstance::EState::PendingCreate
					: PlayerInstance::EState::Inactive;
				instance.PlayOnStartConsumed = false;
			}

			if (false == effectivelyEnabled)
			{
				// 비활성/자산 미지정은 backend 자원만 즉시 해제한다. 상태 레코드를 남겨
				// 다음 활성 전환을 명시적으로 감지하고 PlayOnStart를 한 번만 재허용한다.
				if (instance.WasEffectivelyEnabled || instance.Player)
				{
					instance.ResetBackendResources();
					instance.PlayOnStartConsumed = false;
				}
				instance.WasEffectivelyEnabled = false;
				instance.State = PlayerInstance::EState::Inactive;
				return;
			}

			if (false == instance.WasEffectivelyEnabled)
			{
				instance.ResetBackendResources();
				instance.State = PlayerInstance::EState::PendingCreate;
				instance.PlayOnStartConsumed = false;
			}
			instance.WasEffectivelyEnabled = true;

			// 인스턴스 신규 생성.
			if (PlayerInstance::EState::PendingCreate == instance.State)
			{
				if (false == m_assetManager.IsValid()) return;

				AssetMetaData metaData;
				if (false == m_assetManager->GetRegistry().TryGetAsset(player.AudioGuid, metaData))
				{
					instance.State = PlayerInstance::EState::LoadFailed;
					return;
				}

				File::Path resolvedPath;
				if (false == m_assetManager->ResolveAssetPath(metaData.Path, resolvedPath))
				{
					instance.State = PlayerInstance::EState::LoadFailed;
					return;
				}

				const auto u8 = resolvedPath.generic_u8string();
				const std::string utf8Path(reinterpret_cast<const char*>(u8.c_str()), u8.size());
				AudioPlayerDesc desc;
				desc.StreamPathUtf8 = utf8Path.c_str();
				// 버스로 라우팅 — 이게 없으면 전부 Master 로 붙어 카테고리 볼륨이 무의미해진다.
				// 목록에 없는 이름이면 디바이스가 Master 로 떨구고 경고를 한 번 남긴다.
				desc.Bus = m_device->GetBus(player.Bus.c_str());
				OwnerPtr<IAudioPlayer> created = m_device->CreatePlayer(desc);
				if (false == bool(created))
				{
					// 실패를 자연 종료와 분리하고 같은 활성 구간에서는 매 프레임 재시도하지 않는다.
					instance.State = PlayerInstance::EState::LoadFailed;
					return;
				}

				instance.Player = std::move(created);
				instance.State = PlayerInstance::EState::Ready;

				// 시작 옵션 적용.
				instance.Player->SetVolume(player.Volume);
				instance.Player->SetPitch (player.Pitch);
				instance.Player->SetLoop  (player.Loop);
			}

			// 매 프레임 갱신 — 인스턴스별 오버라이드를 player 에 반영.
			if (instance.Player)
			{
				instance.Player->SetVolume(player.Volume);
				instance.Player->SetPitch (player.Pitch);
				instance.Player->SetLoop  (player.Loop);

				// 효과 체인(EffectGuids) 동기.
				if (m_assetManager.IsValid() && m_device.IsValid())
				{
					SyncEffectChain(instance, player);
				}

				if (player.PlayOnStart && false == instance.PlayOnStartConsumed)
				{
					instance.Player->Play();
					instance.PlayOnStartConsumed = true;
				}

				// Is3D 여부와 무관하게 매번 넘긴다. 3D 일 때만 부르면 재생 중에 Is3D 를 끄더라도
				// 공간화가 켜진 채로 남는다(끄는 신호를 아무도 보내지 않으므로).
				AudioSpatialParams spatial;
				spatial.Is3D             = player.Is3D;
				spatial.MinDistance      = player.MinDistance;
				spatial.MaxDistance      = player.MaxDistance;
				spatial.AttenuationModel = player.AttenuationModel;
				spatial.Rolloff          = player.Rolloff;
				instance.Player->SetSpatial(spatial);

				if (player.Is3D)
				{
					// Transform 은 이제 GameObject 의 멤버라 항상 존재.
					instance.Player->SetPosition(ExtractWorldPosition(*owner));
				}

				// 자연 종료는 상태 레코드를 보존한다. 같은 활성 구간에서 PlayOnStart가
				// 다시 생성되지 않고 Disable/Enable 또는 AudioGuid 변경 때만 재무장된다.
				if (false == player.Loop && instance.Player->IsEnded())
				{
					instance.ResetBackendResources();
					instance.State = PlayerInstance::EState::Finished;
				}
			}
		});

	// ── 3) AudioPlayer 컴포넌트가 사라진(파괴된) 엔티티의 인스턴스 청소 ─
	for (auto it = m_instances.begin(); it != m_instances.end(); )
	{
		if (it->second.LastSeenFrame != m_frameStamp)
		{
			it->second.ResetBackendResources();
			it = m_instances.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CAudioSystem::OnFinalize(CGameCanvas&)
{
	// 캔버스 종료 — 명시적 순서로 player/effect를 정리하고 상태까지 폐기.
	for (auto& kv : m_instances)
	{
		kv.second.ResetBackendResources();
	}
	m_instances.clear();
}

void CAudioSystem::OnSimulationStop(CGameCanvas&)
{
	// 시뮬레이션 정지 — 재생 중이던 player 를 모두 정지·해제한다.
	// 편집 모드에선 OnUpdate 가 안 돌아 자동 GC 가 없으므로 여기서 명시 정리.
	for (auto& kv : m_instances)
	{
		kv.second.ResetBackendResources();
	}
	m_instances.clear();
}
