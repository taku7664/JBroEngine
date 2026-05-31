#include "pch.h"
#include "AudioSystem.h"

#include "Core/Asset/AudioAsset.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Audio/AudioTypes.h"
#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioPlayer.h"
#include "Core/Audio/MiniAudio/MiniAudioDevice.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

namespace
{
	AudioVec3 ExtractWorldPosition(const CScene& scene, EntityId entity)
	{
		const Matrix3x2 m = GetWorldTransform(scene, entity);
		AudioVec3 v;
		v.X = m.Dx;   // translation x
		v.Y = m.Dy;   // translation y
		v.Z = 0.0f;
		return v;
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

void CAudioSystem::OnUpdate(CScene& scene)
{
	if (false == m_device.IsValid())
	{
		return;
	}

	// ── 1) Listener — 첫 번째 활성 청취자만 사용 ────────────────────────
	SafePtr<IAudioListener> primary = m_device->GetPrimaryListener();
	bool listenerSet = false;
	scene.ForEach<GameObject, Transform2D, AudioListener>(
		[&](EntityId entity, const GameObject& go, const Transform2D&, AudioListener& listener)
		{
			if (listenerSet) return;
			if (false == go.IsActive || false == listener.IsEnabled) return;
			if (primary.IsValid())
			{
				primary->SetPosition(ExtractWorldPosition(scene, entity));
				primary->SetMasterVolume(listener.MasterVolume);
			}
			listenerSet = true;
		});

	// ── 2) Player — 컴포넌트별 인스턴스 생성/동기/해제 ─────────────────
	std::unordered_set<EntityId> seen;

	scene.ForEach<GameObject, AudioPlayer>(
		[&](EntityId entity, const GameObject& go, AudioPlayer& player)
		{
			seen.insert(entity);

			const bool effectivelyEnabled = go.IsActive && player.IsEnabled
				&& false == player.AudioGuid.IsNull();

			auto it = m_instances.find(entity);

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

				it = m_instances.emplace(entity, std::move(inst)).first;
			}

			// 매 프레임 갱신 — 인스턴스별 오버라이드를 player 에 반영.
			if (it->second.Player)
			{
				it->second.Player->SetVolume(player.Volume);
				it->second.Player->SetPitch (player.Pitch);
				it->second.Player->SetLoop  (player.Loop);
				if (player.Is3D)
				{
					AudioSpatialParams spatial;
					spatial.Is3D        = true;
					spatial.MinDistance = player.MinDistance;
					spatial.MaxDistance = player.MaxDistance;
					it->second.Player->SetSpatial(spatial);

					if (scene.HasComponent<Transform2D>(entity))
					{
						it->second.Player->SetPosition(ExtractWorldPosition(scene, entity));
					}
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
