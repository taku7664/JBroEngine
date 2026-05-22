#pragma once

class CAssetBrowserTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CAssetBrowserTool() = default;
private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

};

