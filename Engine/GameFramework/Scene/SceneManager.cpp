#include "pch.h"
#include "SceneManager.h"

#include "Core/ScriptCore.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/AssetRef.inl"   // LoadAsset 반환 AssetRef 의 복사/이동/소멸 인스턴스화
#include "Core/Asset/CanvasAsset.h"  // 캔버스 자산 텍스트(전환)
#include "Core/Asset/AssetTypeRules.h"
#include "Core/Logging/LoggerInternal.h"
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

void CSceneManager::FlushPendingCanvasTransition()
{
	if (m_pendingCanvasTransition.empty())
	{
		return;
	}

	// 먼저 비운다 — 실패해도 매 프레임 같은 전환을 다시 시도하지 않게.
	const std::string guidText = std::move(m_pendingCanvasTransition);
	m_pendingCanvasTransition.clear();

	CGameScene* canvas = m_canvas.Get();
	if (nullptr == canvas || false == Script.AssetManager.IsValid())
	{
		return;
	}

	// guid 하나로 에디터·패키지가 같은 경로를 탄다 — 경로 폴백도, 이름 등록도 필요 없다.
	//
	// 실패는 두 갈래고 원인이 전혀 다르다 — 뭉뚱그리면 어디를 고쳐야 할지 알 수 없다.
	//   · 로드 실패  = guid 가 레지스트리에 없거나(임포트 안 됨/`.jmeta` 문제) 파일을 못 읽음
	//   · 타입 불일치 = 그 guid 가 캔버스가 아닌 다른 자산을 가리킴
	AssetRef<IAsset> asset = Script.AssetManager->LoadAsset(AssetGuid(guidText));
	if (false == asset.IsValid())
	{
		CSystemLog::Error(std::string("Canvas transition failed (asset not loaded - unregistered guid or unreadable file): ") + guidText);
		return;
	}

	// 타입 판정은 **자산 타입 enum** 으로 한다 — RTTI(dynamic_cast)를 쓸 이유가 없다.
	// 타입→클래스는 로더 등록이 지키는 계약이고(Scene = CCanvasAssetLoader = CCanvasAsset),
	// 검증을 마친 뒤 StaticAssetRefCast 로 내려가는 게 이 엔진의 정식 경로다.
	// AssetRef 로 받는 것도 계약이다 — strong ref 가 수명을 잡는다(로우 포인터로 빼지 않는다).
	if (EAssetType::Scene != asset->GetAssetType())
	{
		CSystemLog::Error(std::string("Canvas transition failed (asset is not a canvas): ") + guidText
			+ ", type=" + CAssetTypeRules::GetTypeName(asset->GetAssetType()));
		return;
	}

	AssetRef<CCanvasAsset> canvasAsset = StaticAssetRefCast<CCanvasAsset>(asset);
	if (false == canvasAsset.IsValid())
	{
		CSystemLog::Error(std::string("Canvas transition failed (canvas asset cast failed): ") + guidText);
		return;
	}

	const std::string text(canvasAsset->GetText());
	CSceneSerializer serializer;
	std::vector<SafePtr<CGameLayer>> inheritedLayers;
	if (ESceneSerializeResult::Success != serializer.TransitionFromText(*canvas, text.c_str(), inheritedLayers))
	{
		CSystemLog::Error(std::string("Canvas transition failed (load error): ") + guidText);
		return;
	}

	// 캔버스 이름은 여전히 파일 키다(에디터 문서 키·Ref<GameScene> 해석) — 자산 메타에서 꺼낸다.
	SetCanvasName(canvasAsset->GetMetaData().Path.generic_string().c_str());
	RefreshReferencedAssets();

	// 훅은 **맨 마지막**이다 — 승계 스크립트가 여기서 새 캔버스를 기준으로 자기를 다시 잡는데
	// (스폰 재배치·속도 리셋·UI 정리), 이름이나 참조 에셋이 아직 옛 것이면 그 판단이 틀린다.
	canvas->DispatchCanvasChangedToScripts(inheritedLayers);
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
		// 이름도 함께 — 재생 중에 캔버스를 전환하면 이름이 목적지 것으로 바뀌는데, 정지 시
		// 내용만 되돌리면 "내용은 A, 이름은 B" 가 된다(Ref<GameScene> 해석·에디터 문서 키가
		// 어긋난다). 스냅샷은 캔버스 상태 전체다.
		m_playModeCanvasName = m_canvasName;
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

	// 재생 중 예약된 전환은 버린다 — 정지가 재생 직전 상태로 되돌리는 마당에 그 뒤에
	// 전환이 터지면 편집 중인 캔버스가 남의 것으로 바뀐다.
	m_pendingCanvasTransition.clear();

	// 스냅샷 복원 전에 시스템 정리 — 시뮬 중 시작된 사운드 등을 해제한다.
	// (편집 모드에선 시스템 Update 가 안 돌아 player GC 가 일어나지 않으므로 명시 정리 필요.)
	if (ESceneSimulationState::Edit != m_simulationState && nullptr != canvas)
	{
		canvas->NotifySimulationStop();
	}

	if (ESceneSimulationState::Edit != m_simulationState && nullptr != canvas && false == m_playModeSnapshot.empty())
	{
		CSceneSerializer serializer;
		// 전체 교체다(전환 아님) — 재생 직전으로 되돌리는 것이지 승계할 게 없다.
		serializer.DeserializeFromText(*canvas, m_playModeSnapshot.c_str());
		SetCanvasName(m_playModeCanvasName.c_str());
		// 재생 중 전환했다면 지금 들고 있는 건 목적지 캔버스의 리소스다 — 되돌린 내용에 맞춘다.
		RefreshReferencedAssets();
	}

	m_playModeSnapshot.clear();
	m_playModeCanvasName.clear();
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

	// 전환은 시뮬 순회가 전부 끝난 뒤 — 캔버스를 갈아엎는 건 순회 중인 오브젝트를
	// 발밑에서 없애는 일이다. 예약(RequestCanvasTransition)과 실행을 갈라 놓은 이유.
	FlushPendingCanvasTransition();
}

void CSceneManager::Clear()
{
	m_canvas.Reset();
	m_canvasName.clear();
	m_pendingCanvasTransition.clear();
	m_simulationState = ESceneSimulationState::Edit;
	m_playModeSnapshot.clear();
	m_playModeCanvasName.clear();
}
