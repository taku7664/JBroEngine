#pragma once

class CInspectorTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CInspectorTool() = default;

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:

};

