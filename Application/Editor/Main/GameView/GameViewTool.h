#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"

class CGameViewTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CGameViewTool() = default;

private:
	void OnCreate()     override;
	void OnDestroy()    override;
	void OnUpdate()     override;
	void OnRenderStay() override;

	// 게임 입력 게이팅 — GameView 패널 포커스 시에만 게임 InputSystem 폴링/디스패치 허용.
	// (OnFocusExit 는 패널 숨김 시에도 발화되므로 별도 처리 불필요.)
	void OnFocusEnter() override;
	void OnFocusExit()  override;
};
