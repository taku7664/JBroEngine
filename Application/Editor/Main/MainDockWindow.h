#pragma once

class CMainDockWindow : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;
	virtual ~CMainDockWindow() = default;

private:
	void OnCreate() override;
	void OnMenuBar() override;
};

