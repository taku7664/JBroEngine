#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"

class CSceneViewTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CSceneViewTool() = default;

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:

};

