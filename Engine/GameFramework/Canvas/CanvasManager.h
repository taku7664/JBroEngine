#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "GameFramework/Canvas/CanvasTypes.h"

#include <string>
#include <vector>

class CGameCanvas;

// ─────────────────────────────────────────────────────────────────────────────
//  CCanvasManager — 런타임 캔버스(구 캔버스) **1개**를 소유한다.
//
//  캔버스 전환은 "파괴→생성"이 아니라 이 하나에 diff 를 적용하는 것이다(설계 12차).
//  인스턴스가 여러 개면 승계 레이어를 다른 풀로 옮기는 "이주"가 되어 버리고, 그건 설계가
//  명시적으로 기각한 것이다(SafePtr/guid/Ref/스크립트 인스턴스가 전부 깨진다). 그래서
//  여기는 목록이 아니라 캔버스 하나를 든다 — 파일을 열 때 인스턴스는 그대로 두고 내용만
//  갈아끼운다(풀·시스템·SafePtr 유지).
// ─────────────────────────────────────────────────────────────────────────────
class CCanvasManager final : public EnableSafeFromThis<CCanvasManager>
{
public:
	CCanvasManager() = default;
	~CCanvasManager();
	CCanvasManager(const CCanvasManager&) = delete;
	CCanvasManager& operator=(const CCanvasManager&) = delete;
	CCanvasManager(CCanvasManager&&) = delete;
	CCanvasManager& operator=(CCanvasManager&&) = delete;

public:
	// 런타임 캔버스를 보장한다(없으면 생성). 프로세스 수명 내내 같은 인스턴스다.
	CGameCanvas& GetOrCreateCanvas();
	// 캔버스 파일 키 — Ref<Canvas> 해석 키이자 에디터 문서 키. 내용을 교체할 때마다 갱신한다.
	void SetCanvasName(const char* name);

	// ── 캔버스 전환 ───────────────────────────────────────────────────────────
	// 전환 예약. 키는 **캔버스 에셋 guid** 다 — 경로가 아니다.
	//   · 이 엔진의 자산 신원은 guid + `.jmeta` 사이드카다. 경로로 지목하면 파일을 옮기거나
	//     이름만 바꿔도 조용히 깨지는데, `.jmeta` 가 존재하는 이유가 바로 그걸 막는 것이다.
	//   · 경로는 베이스가 여럿이라(프로젝트 상대 / 에셋 루트 상대 / 캔버스 이름 키) 어느 쪽인지
	//     계속 헷갈린다. guid 는 고를 게 없다.
	//   · guid 면 빌드 수집기가 "이 캔버스가 저 캔버스로 갈 수 있다"를 실제 의존 간선으로 본다.
	//   · 에디터/패키지 분기도 사라진다 — LoadAsset(guid) 하나로 양쪽 다 된다.
	// 스크립트는 `JPROP() Ref<CCanvasAsset> Target;` 으로 저작하고 `Target.GuidText()` 를 넘긴다
	// (RefBase 는 고정 char 버퍼라 호스트↔게임 DLL 경계를 넘어도 안전하다).
	//
	// **인라인이어야 한다** — 게임 스크립트 DLL 이 Script.CanvasManager 로 이걸 부른다.
	// out-of-line 이면 CanvasManager.obj 가 링크 클로저에 끌려오고 CanvasSerializer.obj →
	// yaml-cpp 연쇄로 DLL 링크가 깨진다. 그래서 여기서는 guid 만 적어 둔다.
	//
	// 실행은 호스트가 프레임 끝에 한다(설계 8차의 "프레임 끝 지연 실행"이기도 하다) —
	// 스크립트 순회 도중 캔버스를 갈아엎으면 순회 중인 오브젝트가 발밑에서 사라진다.
	// 같은 프레임에 두 번 부르면 마지막 것만 남는다(전환 목적지는 하나뿐이다).
	void RequestCanvasTransition(const char* canvasAssetGuidText)
	{
		m_pendingCanvasTransition = canvasAssetGuidText ? canvasAssetGuidText : "";
	}
	bool HasPendingCanvasTransition() const { return false == m_pendingCanvasTransition.empty(); }

	// ── 레이어 런타임 로드 ────────────────────────────────────────────────────
	// `.jlayer` 에셋을 지금 캔버스로 로드 예약한다(목록 맨 위 = 컴포짓 최전면). 키는 레이어 에셋
	// guid — 전환과 같은 이유다(경로는 베이스가 여럿, guid 는 고를 게 없다). 인라인이어야 하는
	// 것도 같다 — 게임 DLL 이 Script.CanvasManager 로 부른다(out-of-line 이면 CanvasSerializer →
	// yaml-cpp 가 DLL 링크 클로저에 끌려온다). 그래서 여기서는 guid 만 쌓아 둔다.
	// 실행은 호스트가 프레임 끝에 한다 — 스크립트 순회 중 레이어를 추가하면 실행순서 캐시가
	// 순회 도중 흔들린다. 언로드는 GetActiveCanvas()->DestroyLayer(layer) 로 한다(이미 공개).
	void RequestLayerLoad(const char* layerAssetGuidText)
	{
		if (layerAssetGuidText && '\0' != layerAssetGuidText[0])
		{
			m_pendingLayerLoads.emplace_back(layerAssetGuidText);
		}
	}
	bool HasPendingLayerLoads() const { return false == m_pendingLayerLoads.empty(); }

	// 인라인 정의 — 게임 스크립트 DLL 의 Ref<T> 해석(Ref.cpp)이 이 함수를 호출하는데,
	// out-of-line 이면 CanvasManager.obj 가 링크 클로저에 끌려오고, 그 obj 가
	// CCanvasSerializer(PlaySimulation/StopSimulation) → CanvasSerializer.obj → yaml-cpp
	// 를 연쇄로 끌어와 DLL 링크가 깨진다(DLL 은 yaml-cpp 를 링크하지 않음).
	// 헤더 인라인이면 CanvasManager.obj 를 끌어오지 않아 체인이 끊긴다.
	SafePtr<CGameCanvas> GetActiveCanvas() const { return m_canvas.GetSafePtr(); }
	// FindCanvas 도 같은 이유로 인라인 — Ref<Canvas> 해석(Ref.cpp, DLL 링크)이 호출한다.
	// 캔버스는 하나뿐이라 "이름이 맞으면 그것, 아니면 없음"이다. 이름 비교는 여기 보관한
	// 사본으로 한다 — 캔버스에게 물으면 CGameCanvas 완전 타입이 필요해 DLL 에서 깨진다.
	SafePtr<CGameCanvas> FindCanvas(const char* name) const
	{
		if (nullptr == name || m_canvasName != name)
		{
			return nullptr;
		}
		return m_canvas.GetSafePtr();
	}
	// 캔버스의 referenced 에셋을 로드하고 캔버스가 AssetRef(strong)로 보유하게 한다(use-count>0).
	// 이미 보유 중이면 no-op — 내용 교체 후에는 RefreshReferencedAssets 를 쓸 것.
	void AcquireReferencedAssets(CGameCanvas& canvas) const;
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
	ECanvasSimulationState GetSimulationState() const;
	void Update();
	void Clear();

private:
	// 예약된 전환을 실행한다(프레임 끝). 캔버스 자산을 읽어 diff 를 적용하고 이름·리소스를
	// 갱신한다. 실패하면 현재 캔버스를 그대로 둔다 — 반쯤 전환된 캔버스를 만들지 않는다.
	void FlushPendingCanvasTransition();

	// 예약된 레이어 로드를 실행한다(프레임 끝). 각 `.jlayer` 를 읽어 캔버스에 레이어를 추가하고,
	// 그 레이어 파일의 ReferencedAssets 를 로드해 레이어 수명에 묶어 보유한다(레이어 파괴 시 해제).
	void FlushPendingLayerLoads();

private:
	OwnerPtr<CGameCanvas> m_canvas;
	// m_canvas->GetName() 의 사본 — FindCanvas 이 헤더 인라인이라 완전 타입을 못 쓴다.
	std::string m_canvasName;
	// 예약된 전환 대상의 캔버스 에셋 guid. guid 를 문자열로 드는 건 이 헤더가 게임 DLL 에도
	// 들어가기 때문이다(File::Guid 는 fs::path 파생 — 경계 밖으로 내보내지 않는다).
	std::string m_pendingCanvasTransition;
	// 예약된 레이어 로드 대상의 `.jlayer` 에셋 guid 들(예약 순서대로 로드). 전환과 같은 이유로
	// 문자열이다 — 이 헤더가 게임 DLL 에도 들어가므로 File::Guid(fs::path 파생)를 두지 않는다.
	std::vector<std::string> m_pendingLayerLoads;
	ECanvasSimulationState m_simulationState = ECanvasSimulationState::Edit;
	std::string m_playModeSnapshot;
	// 스냅샷 시점의 캔버스 이름 — 재생 중 전환하면 이름이 바뀌므로 내용과 함께 되돌린다.
	std::string m_playModeCanvasName;
	float m_fixedAccumulator = 0.0f;
};
