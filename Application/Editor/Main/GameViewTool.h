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
};
