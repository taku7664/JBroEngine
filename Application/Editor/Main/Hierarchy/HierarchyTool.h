#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"
#include "Utillity/File/FilePath.h"

class CLayerTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CLayerTool() = default;

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:
	File::Guid m_selectionAnchorGuid;
};

