#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"

class CHierarchyTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CHierarchyTool() = default;

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:
};

