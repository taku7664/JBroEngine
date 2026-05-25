#pragma once

class CRootDockWindow : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;
	virtual ~CRootDockWindow() = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;
	void OnMenuBar() override;
};
