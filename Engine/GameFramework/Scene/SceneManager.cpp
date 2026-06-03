#include "pch.h"
#include "SceneManager.h"

#include "Core/Core.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Time/Time.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSerializer.h"

CSceneManager::~CSceneManager()
{
	Clear();
}

CScene* CSceneManager::CreateScene(const char* name)
{
	if (nullptr == name || '\0' == name[0])
	{
		return nullptr;
	}

	auto it = m_scenes.find(name);
	if (it != m_scenes.end())
	{
		return it->second.Get();
	}

	OwnerPtr<CScene> scene = MakeOwnerPtr<CScene>();
	CScene* rawScene = scene.Get();
	m_scenes.emplace(name, std::move(scene));
	if (false == m_activeScene.IsValid())
	{
		m_activeScene = rawScene->SafeFromThis();
	}
	return rawScene;
}

bool CSceneManager::DestroyScene(const char* name)
{
	if (nullptr == name)
	{
		return false;
	}

	auto it = m_scenes.find(name);
	if (it == m_scenes.end())
	{
		return false;
	}

	if (m_activeScene.TryGet() == it->second.Get())
	{
		m_activeScene = nullptr;
	}

	m_scenes.erase(it);
	return true;
}

bool CSceneManager::SetActiveScene(const char* name)
{
	SafePtr<CScene> scene = FindScene(name);
	if (false == scene.IsValid())
	{
		return false;
	}

	PreloadReferencedAssets(*scene);
	m_activeScene = scene;
	return true;
}

// GetActiveScene() 은 SceneManager.h 에 인라인으로 정의됨(DLL 링크 클로저에서
// SceneManager.obj → SceneSerializer.obj → yaml-cpp 연쇄 풀을 끊기 위함).

SafePtr<CScene> CSceneManager::FindScene(const char* name) const
{
	if (nullptr == name)
	{
		return nullptr;
	}

	auto it = m_scenes.find(name);
	return it != m_scenes.end() ? it->second.GetSafePtr() : nullptr;
}

void CSceneManager::PreloadReferencedAssets(const CScene& scene) const
{
	if (false == Core::AssetManager.IsValid())
	{
		return;
	}

	for (const AssetGuid& guid : scene.GetReferencedAssets())
	{
		Core::AssetManager->LoadAsset(guid);
	}
}

std::size_t CSceneManager::GetLoadedSceneCount() const
{
	return m_scenes.size();
}

bool CSceneManager::GetLoadedSceneNames(std::vector<std::string>& outNames) const
{
	outNames.clear();
	outNames.reserve(m_scenes.size());
	for (const auto& pair : m_scenes)
	{
		outNames.push_back(pair.first);
	}
	return true;
}

void CSceneManager::DestroyScriptInstances()
{
	for (auto& pair : m_scenes)
	{
		if (pair.second)
		{
			pair.second->DestroyScriptInstances();
		}
	}
}

void CSceneManager::PlaySimulation()
{
	if (ESceneSimulationState::Edit == m_simulationState && m_activeScene)
	{
		CSceneSerializer serializer;
		m_playModeSnapshot.clear();
		serializer.SerializeToText(*m_activeScene, m_playModeSnapshot);
	}

	m_simulationState = ESceneSimulationState::Playing;
}

void CSceneManager::PauseSimulation()
{
	if (ESceneSimulationState::Playing == m_simulationState)
	{
		m_simulationState = ESceneSimulationState::Paused;
	}
}

void CSceneManager::StopSimulation()
{
	// 스냅샷 복원 전에 시스템 정리 — 시뮬 중 시작된 사운드 등을 해제한다.
	// (편집 모드에선 시스템 Update 가 안 돌아 player GC 가 일어나지 않으므로 명시 정리 필요.)
	if (ESceneSimulationState::Edit != m_simulationState && m_activeScene)
	{
		m_activeScene->NotifySimulationStop();
	}

	if (ESceneSimulationState::Edit != m_simulationState && m_activeScene && false == m_playModeSnapshot.empty())
	{
		CSceneSerializer serializer;
		serializer.DeserializeFromText(*m_activeScene, m_playModeSnapshot.c_str());
	}

	m_playModeSnapshot.clear();
	m_simulationState    = ESceneSimulationState::Edit;
	m_fixedAccumulator   = 0.0f;
}

bool CSceneManager::IsSimulationPlaying() const
{
	return ESceneSimulationState::Playing == m_simulationState;
}

bool CSceneManager::IsSimulationPaused() const
{
	return ESceneSimulationState::Paused == m_simulationState;
}

ESceneSimulationState CSceneManager::GetSimulationState() const
{
	return m_simulationState;
}

void CSceneManager::Update()
{
	if (!m_activeScene)
	{
		return;
	}

	const bool isPlaying = IsSimulationPlaying();

	// ── Fixed update loop ────────────────────────────────────────────────────
	// Accumulates scaled delta time and steps the physics + FixedUpdate at a
	// consistent interval regardless of frame rate.
	// Only runs during active simulation (not in Edit or Paused state).
	if (isPlaying && Core::Time)
	{
		const float fixedDelta = Core::Time->GetFixedDeltaSeconds();
		m_fixedAccumulator += Core::Time->GetDeltaSeconds();

		// Spiral-of-death guard: cap accumulator to 8 fixed steps.
		// If the game hiccups, we lose fixed-update steps rather than
		// trying to catch up indefinitely and freezing the frame.
		const float maxAccumulator = fixedDelta * 8.0f;
		if (m_fixedAccumulator > maxAccumulator)
		{
			m_fixedAccumulator = maxAccumulator;
		}

		while (m_fixedAccumulator >= fixedDelta)
		{
			m_activeScene->FixedUpdate();
			m_fixedAccumulator -= fixedDelta;
		}
	}

	// ── Variable update ──────────────────────────────────────────────────────
	m_activeScene->Update(isPlaying);
}

void CSceneManager::Clear()
{
	m_activeScene = nullptr;
	m_scenes.clear();
	m_simulationState = ESceneSimulationState::Edit;
	m_playModeSnapshot.clear();
}
