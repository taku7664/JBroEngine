#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>
#include <unordered_map>

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

	// ── 캔버스 전환 ───────────────────────────────────────────────────────────
	// 이름 → 캔버스 에셋 guid 등록(빌드 매니페스트의 빌드 씬 목록). **패키지에는 디스크 경로가
	// 없어 guid 로만 캔버스를 찾을 수 있다.** 에디터는 등록 없이 경로로 찾는다(프로젝트 파일이
	// 그대로 있으므로) — 그래서 이 등록은 런타임 부팅에서만 한다.
	void RegisterCanvas(const char* name, const char* assetGuidText);

	// 전환 예약. 이름 = 캔버스 파일 키(프로젝트 상대 경로, 매니페스트·에디터와 같은 키).
	//
	// **인라인이어야 한다** — 게임 스크립트 DLL 이 Script.SceneManager 로 이걸 부른다.
	// out-of-line 이면 SceneManager.obj 가 링크 클로저에 끌려오고 SceneSerializer.obj →
	// yaml-cpp 연쇄로 DLL 링크가 깨진다. 그래서 여기서는 이름만 적어 둔다.
	//
	// 실행은 호스트가 프레임 끝에 한다(설계 8차의 "프레임 끝 지연 실행"이기도 하다) —
	// 스크립트 순회 도중 캔버스를 갈아엎으면 순회 중인 오브젝트가 발밑에서 사라진다.
	// 같은 프레임에 두 번 부르면 마지막 것만 남는다(전환 목적지는 하나뿐이다).
	void RequestCanvasTransition(const char* canvasName)
	{
		m_pendingCanvasTransition = canvasName ? canvasName : "";
	}
	bool HasPendingCanvasTransition() const { return false == m_pendingCanvasTransition.empty(); }

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
	// 예약된 전환을 실행한다(프레임 끝). 캔버스 파일을 읽어 diff 를 적용하고 이름·리소스를
	// 갱신한다. 실패하면 현재 캔버스를 그대로 둔다 — 반쯤 전환된 캔버스를 만들지 않는다.
	void FlushPendingCanvasTransition();
	// 캔버스 파일 텍스트를 읽는다. 등록된 이름이면 guid(패키지 에셋)로, 아니면 경로로.
	bool ReadCanvasText(const std::string& name, std::string& outText) const;

private:
	OwnerPtr<CGameScene> m_canvas;
	// m_canvas->GetName() 의 사본 — FindScene 이 헤더 인라인이라 완전 타입을 못 쓴다.
	std::string m_canvasName;
	// 이름 → 캔버스 에셋 guid 문자열. guid 를 문자열로 드는 건 이 헤더가 게임 DLL 에도
	// 들어가기 때문이다(File::Guid 는 fs::path 파생 — 경계 밖으로 내보내지 않는다).
	std::unordered_map<std::string, std::string> m_canvasRegistry;
	std::string m_pendingCanvasTransition;
	ESceneSimulationState m_simulationState = ESceneSimulationState::Edit;
	std::string m_playModeSnapshot;
	// 스냅샷 시점의 캔버스 이름 — 재생 중 전환하면 이름이 바뀌므로 내용과 함께 되돌린다.
	std::string m_playModeCanvasName;
	float m_fixedAccumulator = 0.0f;
};
