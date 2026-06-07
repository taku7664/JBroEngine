#pragma once

#include "Editor/Build/GameBuildManager.h"          // CGameBuildManager, GameBuildSnapshot
#include "Editor/LiveCompile/LiveCompileTypes.h"
#include "Engine/Editor/ImWindow/ImDockWindow.h"   // CImDockWindow 기반 클래스
#include "Engine/Editor/ImWindow/IImPopupWindow.h" // IImPopupWindow 시그니처
#include "Engine/Editor/ImWindow/ImWindowContext.h"// PopupHandle, INVALID_POPUP_HANDLE
#include "Engine/Editor/Project/ProjectManager.h"   // AssetReconcileReport
#include "Utillity/File/FilePath.h"
#include <array>
#include <vector>

class CRootDockWindow : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;
	virtual ~CRootDockWindow() = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;
	void OnMenuBar() override;

	// 새 프로젝트 팝업 상태
	void OpenNewProjectPopup(const File::Path& parentFolder);
	void RenderNewProjectPopup(IImPopupWindow& popup);

	// 프로젝트 로드 중 프로그레스 팝업 (X 버튼 없음, 리사이즈 불가, 완료 시 자동 닫힘).
	void EnsureProjectLoadingPopup();
	void RenderProjectLoadingPopup(IImPopupWindow& popup);

	// 프로젝트 로드 직후, 자산 정합성 패스가 무언가를 치유했으면 1회 요약 팝업을 띄운다.
	void MaybeShowReconcileSummary();
	void RenderReconcileSummaryPopup(IImPopupWindow& popup);

	// 메뉴바 우측에 스크립트 빌드 상태(스피너 + 텍스트) 표시
	void DrawLiveCompileMenuBarStatus();

	void StartGameBuild(EBuildTargetPlatform platform);
	// 일괄 빌드: 활성 플랫폼 리스트를 큐에 넣고 순차 빌드한다(직전 빌드 성공 시 다음 트리거).
	void StartBatchGameBuild(const std::vector<EBuildTargetPlatform>& platforms);
	void AdvanceBuildQueueIfReady();
	void OpenBuildBlockedPopup(std::string message);
	void RenderBuildBlockedPopup(IImPopupWindow& popup);
	void EnsureBuildProgressPopup();
	void RenderBuildProgressPopup(IImPopupWindow& popup);

	File::Path m_newProjectParentFolder;
	std::array<char, 128> m_newProjectNameBuf = {};
	std::string m_newProjectError;

	// ── 스크립트 빌드 상태 토스트 ─────────────────────────────────────────────
	// Compiling 동안 스피너 + 경과 시간, 완료/실패 시 잠시 잔존 후 사라진다.
	ELiveCompileState m_lastCompileState        = ELiveCompileState::Idle;
	float             m_compileElapsedSeconds   = 0.0f;
	float             m_resultLingerRemaining   = 0.0f;
	bool              m_resultLingerSuccess     = true;

	// 프로젝트 로딩 팝업 핸들 — 같은 팝업 중복 OpenPopup 방지용.
	PopupHandle       m_loadingPopupHandle      = INVALID_POPUP_HANDLE;

	// 마지막 씬 자동 로드 지연 플래그.
	// LoadProject 는 자산 임포트/스크립트 빌드를 비동기 태스크로 큐잉만 하고 즉시 반환한다.
	// 씬 프리로드(PreloadReferencedAssets)는 메인 스레드 동기 작업이라, 비동기 로드가
	// 끝나기 전에 호출하면 메인 스레드가 막혀 에디터가 프리즈된다(+ 자산이 아직 임포트되지
	// 않아 참조가 풀리지 않음). 따라서 HasLoadingTasks() 가 false 가 된 뒤에 로드한다.
	bool              m_pendingLoadLastScene    = false;

	// 자산 정합성 요약 팝업 1회 표시용 — 프로젝트 로드 전이(false→true) 감지.
	bool              m_wasProjectLoaded        = false;
	AssetReconcileReport m_reconcileSummary     = {};

	// ── 게임 빌드(패키징) ─────────────────────────────────────────────────────
	// StartGameBuild 가 백그라운드 워커를 띄우고, 진행 상황은 OpenPopup 으로 띄운
	// 모달 팝업(m_buildPopupHandle)에서 GetSnapshot() 으로 폴링해 표시한다.
	CGameBuildManager m_buildManager;
	PopupHandle       m_buildPopupHandle        = INVALID_POPUP_HANDLE;
	PopupHandle       m_buildBlockedPopupHandle = INVALID_POPUP_HANDLE;
	std::string       m_buildBlockedMessage;

	// 일괄 빌드 큐. 활성 플랫폼을 순서대로 담고, 직전 빌드가 성공하면 다음 인덱스를 빌드한다.
	// 단일 빌드도 큐에 1개만 넣는 특수 케이스로 통일한다(StartGameBuild).
	std::vector<EBuildTargetPlatform> m_buildQueue;
	std::size_t                       m_buildQueueIndex = 0;
};
