#pragma once

#include "Editor/LiveCompile/LiveCompileTypes.h"
#include "Engine/Editor/ImWindow/ImDockWindow.h"   // CImDockWindow 기반 클래스
#include "Engine/Editor/ImWindow/IImPopupWindow.h" // IImPopupWindow 시그니처
#include "File/FilePath.h"
#include <array>

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

	// 메뉴바 우측에 스크립트 빌드 상태(스피너 + 텍스트) 표시
	void DrawLiveCompileMenuBarStatus();

	File::Path m_newProjectParentFolder;
	std::array<char, 128> m_newProjectNameBuf = {};
	std::string m_newProjectError;

	// ── 스크립트 빌드 상태 토스트 ─────────────────────────────────────────────
	// Compiling 동안 스피너 + 경과 시간, 완료/실패 시 잠시 잔존 후 사라진다.
	ELiveCompileState m_lastCompileState        = ELiveCompileState::Idle;
	float             m_compileElapsedSeconds   = 0.0f;
	float             m_resultLingerRemaining   = 0.0f;
	bool              m_resultLingerSuccess     = true;
};
