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
#include "Core/Audio/MiniAudio/MiniAudioDevice.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"

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
		const AssetMetaData* meta = am.GetRegistry().FindAsset(effectGuid);
		if (nullptr == meta || EAssetType::AudioEffect != meta->Type) return nullptr;

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
	if (instance.EffectGuids == player.EffectGuids)
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
	effects.reserve(player.EffectGuids.size());
	generations.reserve(player.EffectGuids.size());
	chain.reserve(player.EffectGuids.size());

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
	instance.EffectGuids       = player.EffectGuids;
}

void CAudioSystem::OnUpdate(CScene& scene)
{
	if (false == m_device.IsValid())
	{
		return;
	}

	// ── 1) Listener — 첫 번째 활성 청취자만 사용 ────────────────────────
	SafePtr<IAudioListener> primary = m_device->GetPrimaryListener();
	bool listenerSet = false;
	scene.ForEach<AudioListener>(
		[&](AudioListener& listener)
		{
			if (listenerSet) return;
			CGameObject* owner = listener.GetOwner();
			if (nullptr == owner || false == owner->IsActive || false == listener.IsEnabled) return;
			if (primary.IsValid())
			{
				primary->SetPosition(ExtractWorldPosition(*owner));
				primary->SetMasterVolume(listener.MasterVolume);
			}
			listenerSet = true;
		});

	// ── 2) Player — 컴포넌트별 인스턴스 생성/동기/해제 ─────────────────
	std::unordered_set<const void*> seen;

	scene.ForEach<AudioPlayer>(
		[&](AudioPlayer& player)
		{
			CGameObject* owner = player.GetOwner();
			if (nullptr == owner) return;

			const void* key = &player;
			seen.insert(key);

			const bool effectivelyEnabled = owner->IsActive && player.IsEnabled
				&& false == player.AudioGuid.IsNull();

			auto it = m_instances.find(key);

			if (false == effectivelyEnabled)
			{
				// 비활성/자산 미지정 → 인스턴스가 있다면 즉시 해제 (자원 누수 방지).
				if (it != m_instances.end())
				{
					if (it->second.Player) it->second.Player->Stop();
					m_instances.erase(it);
				}
				return;
			}

			// 자산이 바뀌었다면 기존 인스턴스를 버리고 재생성.
			if (it != m_instances.end() && it->second.SourceGuid != player.AudioGuid)
			{
				if (it->second.Player) it->second.Player->Stop();
				m_instances.erase(it);
				it = m_instances.end();
			}

			// 인스턴스 신규 생성.
			if (it == m_instances.end())
			{
				if (false == m_assetManager.IsValid()) return;

				const AssetMetaData* metaData = m_assetManager->GetRegistry().FindAsset(player.AudioGuid);
				if (nullptr == metaData) return;

				File::Path resolvedPath;
				if (false == m_assetManager->ResolveAssetPath(metaData->Path, resolvedPath)) return;

				// MiniAudioDevice 의 편의 메서드 사용 — 백엔드 비-mini 인 경우 stub player 폴백.
				OwnerPtr<IAudioPlayer> created;
#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
				if (CMiniAudioDevice* mini = dynamic_cast<CMiniAudioDevice*>(m_device.TryGet()))
				{
					const auto u8 = resolvedPath.generic_u8string();
					const std::string utf8Path(reinterpret_cast<const char*>(u8.c_str()), u8.size());
					created = mini->CreatePlayerFromFile(utf8Path.c_str());
				}
#endif
				if (false == bool(created))
				{
					// 백엔드가 desc 기반만 제공하는 경우의 폴백 — 빈 player.
					AudioPlayerDesc desc;
					created = m_device->CreatePlayer(desc);
				}
				if (false == bool(created)) return;

				PlayerInstance inst;
				inst.Player     = std::move(created);
				inst.SourceGuid = player.AudioGuid;

				// 시작 옵션 적용.
				inst.Player->SetVolume(player.Volume);
				inst.Player->SetPitch (player.Pitch);
				inst.Player->SetLoop  (player.Loop);
				if (player.PlayOnStart)
				{
					inst.Player->Play();
				}

				it = m_instances.emplace(key, std::move(inst)).first;
			}

			// 매 프레임 갱신 — 인스턴스별 오버라이드를 player 에 반영.
			if (it->second.Player)
			{
				it->second.Player->SetVolume(player.Volume);
				it->second.Player->SetPitch (player.Pitch);
				it->second.Player->SetLoop  (player.Loop);

				// 효과 체인(EffectGuids) 동기.
				if (m_assetManager.IsValid() && m_device.IsValid())
				{
					SyncEffectChain(it->second, player);
				}

				if (player.Is3D)
				{
					AudioSpatialParams spatial;
					spatial.Is3D        = true;
					spatial.MinDistance = player.MinDistance;
					spatial.MaxDistance = player.MaxDistance;
					it->second.Player->SetSpatial(spatial);

					// Transform 은 이제 GameObject 의 멤버라 항상 존재.
					it->second.Player->SetPosition(ExtractWorldPosition(*owner));
				}

				// non-loop 자산이 끝까지 재생됐다면 인스턴스 해제 — GC.
				if (false == player.Loop && it->second.Player->IsEnded())
				{
					it->second.Player->Stop();
					m_instances.erase(it);
				}
			}
		});

	// ── 3) AudioPlayer 컴포넌트가 사라진(파괴된) 엔티티의 인스턴스 청소 ─
	for (auto it = m_instances.begin(); it != m_instances.end(); )
	{
		if (seen.find(it->first) == seen.end())
		{
			if (it->second.Player) it->second.Player->Stop();
			it = m_instances.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CAudioSystem::OnFinalize(CScene&)
{
	// 씬 종료 — 모든 player 정리.
	for (auto& kv : m_instances)
	{
		if (kv.second.Player) kv.second.Player->Stop();
	}
	m_instances.clear();
}

void CAudioSystem::OnSimulationStop(CScene&)
{
	// 시뮬레이션 정지 — 재생 중이던 player 를 모두 정지·해제한다.
	// 편집 모드에선 OnUpdate 가 안 돌아 자동 GC 가 없으므로 여기서 명시 정리.
	for (auto& kv : m_instances)
	{
		if (kv.second.Player) kv.second.Player->Stop();
	}
	m_instances.clear();
}
