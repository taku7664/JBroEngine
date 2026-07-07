#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImWindow.h"

class CEditorStatisticsTool final : public CImWindow
{
public:
	using CImWindow::CImWindow;
	~CEditorStatisticsTool() override = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;
};

#endif
