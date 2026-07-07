#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImWindow.h"
#include "ThirdParty/imgui/imgui.h"

class CShortcutReferenceTool final : public CImWindow
{
public:
	using CImWindow::CImWindow;
	~CShortcutReferenceTool() override = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;

private:
	ImGuiTextFilter m_filter;
};

#endif
