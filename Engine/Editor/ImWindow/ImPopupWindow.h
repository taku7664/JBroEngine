#pragma once
#include "IImPopupWindow.h"

#include <functional>
#include <string>

#include "Utillity/BitFlag.h"
#include "ThirdParty/imgui/imgui.h"   // ImVec2

class CImPopupWindow : public IImPopupWindow
{
public:
	CImPopupWindow(PopupHandle handle, const ImPopupDesc& desc);
	~CImPopupWindow();

public:
	// 매 프레임 호출.  팝업이 더 이상 살아있지 않으면 false 반환.
	bool Render();

	void             Close()     override;
	PopupHandle      GetHandle() const override { return m_handle; }
	std::string_view GetId()     const override { return m_id; }
	bool             IsAlive()   const override { return m_bIsOpen; }

private:
	PopupHandle	m_handle;
	std::string	m_id;
	bool		m_bIsRendered;
	bool		m_bIsOpen;
	ImVec2		m_initSize;
	std::string m_title;
	BitFlag		m_flags;
	std::function<void(IImPopupWindow&)> m_renderEnterFunc;
	std::function<void(IImPopupWindow&)> m_renderStayFunc;
	std::function<void(IImPopupWindow&)> m_renderExitFunc;
};
