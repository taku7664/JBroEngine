#pragma once
#include "IImPopupWindow.h"

class CImPopupWindow : public IImPopupWindow
{
public:
	CImPopupWindow(const ImPopupDesc& desc);
	~CImPopupWindow();

public:
	bool Render();

	void Close() override;

private:
	bool		m_bIsRendered;
	bool		m_bIsOpen;
	ImVec2		m_initSize;
	std::string m_title;
	BitFlag		m_flags;
	std::function<void(IImPopupWindow&)> m_renderEnterFunc;
	std::function<void(IImPopupWindow&)> m_renderStayFunc;
	std::function<void(IImPopupWindow&)> m_renderExitFunc;
};

