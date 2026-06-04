#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

class CScene;

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
	CScene* CreateScene(const char* name);
	bool DestroyScene(const char* name);
	bool SetActiveScene(const char* name);
	// 인라인 정의 — 게임 스크립트 DLL 의 Ref<T> 해석(Ref.cpp)이 이 함수를 호출하는데,
	// out-of-line 이면 SceneManager.obj 가 링크 클로저에 끌려오고, 그 obj 가
	// CSceneSerializer(PlaySimulation/StopSimulation) → SceneSerializer.obj → yaml-cpp
	// 를 연쇄로 끌어와 DLL 링크가 깨진다(DLL 은 yaml-cpp 를 링크하지 않음).
	// 헤더 인라인이면 SceneManager.obj 를 끌어오지 않아 체인이 끊긴다.
	SafePtr<CScene> GetActiveScene() const { return m_activeScene; }
	SafePtr<CScene> FindScene(const char* name) const;
	// 씬의 referenced 에셋을 로드하고 그 씬이 AssetRef(strong)로 보유하게 한다(use-count>0).
	// active 전환(SetActiveScene)에서 호출된다. 씬이 이미 보유 중이면 no-op.
	void AcquireReferencedAssets(CScene& scene) const;
	std::size_t GetLoadedSceneCount() const;
	bool GetLoadedSceneNames(std::vector<std::string>& outNames) const;
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
	std::unordered_map<std::string, OwnerPtr<CScene>> m_scenes;
	SafePtr<CScene> m_activeScene;
	ESceneSimulationState m_simulationState = ESceneSimulationState::Edit;
	std::string m_playModeSnapshot;
	float m_fixedAccumulator = 0.0f;
};
