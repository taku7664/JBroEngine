#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>

class CGameScene;

// ─────────────────────────────────────────────────────────────────────────────
//  CSceneManager — 런타임 캔버스(구 씬) **1개**를 소유한다.
//
//  캔버스 전환은 "파괴→생성"이 아니라 이 하나에 diff 를 적용하는 것이다(설계 12차).
//  인스턴스가 여러 개면 승계 레이어를 다른 풀로 옮기는 "이주"가 되어 버리고, 그건 설계가
//  명시적으로 기각한 것이다(SafePtr/guid/Ref/스크립트 인스턴스가 전부 깨진다). 그래서
//  여기는 목록이 아니라 캔버스 하나를 든다 — 파일을 열 때 인스턴스는 그대로 두고 내용만
//  갈아끼운다(풀·시스템·SafePtr 유지).
// ─────────────────────────────────────────────────────────────────────────────
class CSceneManager final : public EnableSafeFromThis<CSceneManager>
{
public:
	CSceneManager() = default;
	~CSceneManager();
	CSceneManager(const CSceneManager&) = delete;
	CSceneManager& operator=(const CSceneManager&) = delete;
	CSceneManager(CSceneManager&&) = delete;
	CSceneManager& operator=(CSceneManager&&) = delete;

public:
	// 런타임 캔버스를 보장한다(없으면 생성). 프로세스 수명 내내 같은 인스턴스다.
	CGameScene& GetOrCreateCanvas();
	// 캔버스 파일 키 — Ref<GameScene> 해석 키이자 에디터 문서 키. 내용을 교체할 때마다 갱신한다.
	void SetCanvasName(const char* name);

	// 인라인 정의 — 게임 스크립트 DLL 의 Ref<T> 해석(Ref.cpp)이 이 함수를 호출하는데,
	// out-of-line 이면 SceneManager.obj 가 링크 클로저에 끌려오고, 그 obj 가
	// CSceneSerializer(PlaySimulation/StopSimulation) → SceneSerializer.obj → yaml-cpp
	// 를 연쇄로 끌어와 DLL 링크가 깨진다(DLL 은 yaml-cpp 를 링크하지 않음).
	// 헤더 인라인이면 SceneManager.obj 를 끌어오지 않아 체인이 끊긴다.
	SafePtr<CGameScene> GetActiveScene() const { return m_canvas.GetSafePtr(); }
	// FindScene 도 같은 이유로 인라인 — Ref<GameScene> 해석(Ref.cpp, DLL 링크)이 호출한다.
	// 캔버스는 하나뿐이라 "이름이 맞으면 그것, 아니면 없음"이다. 이름 비교는 여기 보관한
	// 사본으로 한다 — 캔버스에게 물으면 CGameScene 완전 타입이 필요해 DLL 에서 깨진다.
	SafePtr<CGameScene> FindScene(const char* name) const
	{
		if (nullptr == name || m_canvasName != name)
		{
			return nullptr;
		}
		return m_canvas.GetSafePtr();
	}
	// 캔버스의 referenced 에셋을 로드하고 캔버스가 AssetRef(strong)로 보유하게 한다(use-count>0).
	// 이미 보유 중이면 no-op — 내용 교체 후에는 RefreshReferencedAssets 를 쓸 것.
	void AcquireReferencedAssets(CGameScene& scene) const;
	// 캔버스 내용을 교체한 직후 호출한다. 새 목록을 acquire 한 **다음** 이전 보유분을 놓는다 —
	// 순서가 뒤집히면 두 캔버스가 공유하는 에셋의 use-count 가 0 으로 떨어져 unload/GC 됐다가
	// 곧바로 다시 로드된다.
	void RefreshReferencedAssets();
	void DestroyScriptInstances();
	void PlaySimulation();
	void PauseSimulation();
	void StopSimulation();
	bool IsSimulationPlaying() const;
	bool IsSimulationPaused() const;
	ESceneSimulationState GetSimulationState() const;
	void Update();
	void Clear();

private:
	OwnerPtr<CGameScene> m_canvas;
	// m_canvas->GetName() 의 사본 — FindScene 이 헤더 인라인이라 완전 타입을 못 쓴다.
	std::string m_canvasName;
	ESceneSimulationState m_simulationState = ESceneSimulationState::Edit;
	std::string m_playModeSnapshot;
	float m_fixedAccumulator = 0.0f;
};
