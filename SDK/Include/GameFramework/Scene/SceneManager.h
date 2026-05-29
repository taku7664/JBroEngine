#pragma once

#include "Utillity/SafePtr.h"
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
	SafePtr<CScene> GetActiveScene() const;
	SafePtr<CScene> FindScene(const char* name) const;
	// 씬이 참조하는 모든 에셋을 동기적으로 로드. 호출 후엔 모든 에셋이 메모리에 상주.
	void PreloadReferencedAssets(const CScene& scene) const;
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
