#include "pch.h"
#include "SceneManager.h"

#include "Core/ScriptCore.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/AssetRef.inl"   // LoadAsset 반환 AssetRef 의 복사/이동/소멸 인스턴스화
#include "Core/Time/Time.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneSerializer.h"

CSceneManager::~CSceneManager()
{
	Clear();
}

CGameScene& CSceneManager::GetOrCreateCanvas()
{
	if (nullptr == m_canvas.Get())
	{
		m_canvas = MakeOwnerPtr<CGameScene>();
		m_canvas->SetName(m_canvasName.c_str());
	}
	return *m_canvas.Get();
}

void CSceneManager::SetCanvasName(const char* name)
{
	m_canvasName = name ? name : "";
	if (nullptr != m_canvas.Get())
	{
		// Ref<GameScene> 해석 키 — 여기 사본과 캔버스 쪽이 어긋나면 해석이 빗나간다.
		m_canvas->SetName(m_canvasName.c_str());
	}
}

// GetActiveScene() 은 SceneManager.h 에 인라인으로 정의됨(DLL 링크 클로저에서
// SceneManager.obj → SceneSerializer.obj → yaml-cpp 연쇄 풀을 끊기 위함).

void CSceneManager::AcquireReferencedAssets(CGameScene& scene) const
{
	if (scene.HasLoadedAssets())
	{
		return; // 이미 보유 중(중복 active 등) — 재로드 불필요.
	}
	if (false == Script.AssetManager.IsValid())
	{
		return;
	}

	// referenced 에셋을 LoadAsset 으로 로드하고, 반환된 AssetRef(strong)를 씬이 보유한다.
	// AssetRef 가 살아있는 동안 use-count>0 → 자산이 unload/GC 되지 않는다.
	const std::vector<AssetGuid>& referenced = scene.GetReferencedAssets();
	std::vector<AssetRef<IAsset>> loaded;
	loaded.reserve(referenced.size());
	for (const AssetGuid& guid : referenced)
	{
		AssetRef<IAsset> ref = Script.AssetManager->LoadAsset(guid);
		if (ref.IsValid())
		{
			loaded.push_back(std::move(ref));
		}
	}
	scene.SetLoadedAssets(std::move(loaded));
}

void CSceneManager::RefreshReferencedAssets()
{
	CGameScene* canvas = m_canvas.Get();
	if (nullptr == canvas)
	{
		return;
	}

	// 이전 보유분을 손에 든 채(use-count 유지) 새 목록을 acquire 하고, 그 뒤에 놓는다.
	// 이 순서 덕에 교체 전후가 공유하는 에셋은 count 가 0 을 거치지 않는다 — 이전 캔버스에만
	// 있던 에셋만 여기서 0 이 되어 GC 대상이 된다.
	std::vector<AssetRef<IAsset>> previous = canvas->TakeLoadedAssets();
	AcquireReferencedAssets(*canvas);
	previous.clear();
}

void CSceneManager::DestroyScriptInstances()
{
	if (CGameScene* canvas = m_canvas.Get())
	{
		canvas->DestroyScriptInstances();
	}
}

void CSceneManager::PlaySimulation()
{
	CGameScene* canvas = m_canvas.Get();
	if (ESceneSimulationState::Edit == m_simulationState && nullptr != canvas)
	{
		CSceneSerializer serializer;
		m_playModeSnapshot.clear();
		serializer.SerializeToText(*canvas, m_playModeSnapshot);
	}

	if (nullptr != canvas && Script.Reflection.IsValid())
	{
		canvas->ReserveScriptMemoryForCurrentScripts(*Script.Reflection);
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
	CGameScene* canvas = m_canvas.Get();

	// 스냅샷 복원 전에 시스템 정리 — 시뮬 중 시작된 사운드 등을 해제한다.
	// (편집 모드에선 시스템 Update 가 안 돌아 player GC 가 일어나지 않으므로 명시 정리 필요.)
	if (ESceneSimulationState::Edit != m_simulationState && nullptr != canvas)
	{
		canvas->NotifySimulationStop();
	}

	if (ESceneSimulationState::Edit != m_simulationState && nullptr != canvas && false == m_playModeSnapshot.empty())
	{
		CSceneSerializer serializer;
		serializer.DeserializeFromText(*canvas, m_playModeSnapshot.c_str());
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
	CGameScene* canvas = m_canvas.Get();
	if (nullptr == canvas)
	{
		return;
	}

	const bool isPlaying = IsSimulationPlaying();

	// ── Fixed update loop ────────────────────────────────────────────────────
	// Accumulates scaled delta time and steps the physics + FixedUpdate at a
	// consistent interval regardless of frame rate.
	// Only runs during active simulation (not in Edit or Paused state).
	if (isPlaying && Script.Time)
	{
		const float fixedDelta = Script.Time->GetFixedDeltaSeconds();
		m_fixedAccumulator += Script.Time->GetDeltaSeconds();

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
			canvas->FixedUpdate();
			m_fixedAccumulator -= fixedDelta;
		}
	}

	// ── Variable update ──────────────────────────────────────────────────────
	canvas->Update(isPlaying);
}

void CSceneManager::Clear()
{
	m_canvas.Reset();
	m_canvasName.clear();
	m_simulationState = ESceneSimulationState::Edit;
	m_playModeSnapshot.clear();
}
