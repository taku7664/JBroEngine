#include "pch.h"
#include "ImPopupWindow.h"

CImPopupWindow::CImPopupWindow( const ImPopupDesc& desc )
	: m_initSize( desc.InitSize )
	, m_title( desc.Title )
	, m_flags( desc.Flags )
	, m_renderEnterFunc( desc.OnRenderEnterFunc )
	, m_renderStayFunc( desc.OnRenderStayFunc )
	, m_renderExitFunc( desc.OnRenderExitFunc )
	, m_bIsRendered( false )
	, m_bIsOpen(true)
{
}

CImPopupWindow::~CImPopupWindow()
{
}

bool CImPopupWindow::Render()
{
	if (false == m_bIsRendered)
	{
		ImGui::OpenPopup(m_title.c_str());
	}
	if (false == m_flags[ImGuiWindowFlags_AlwaysAutoResize])
    {
        ImGui::SetNextWindowSize( m_initSize );
    }
	if (ImGui::BeginPopupModal( m_title.c_str(), &m_bIsOpen , m_flags.Get() ))
    {
		if ( m_renderEnterFunc && false == m_bIsRendered )
		{
			m_renderEnterFunc( *this );
		}
        if ( m_renderStayFunc )
		{
			m_renderStayFunc( *this );
		}
        ImGui::EndPopup();
    }
	if ( false == m_bIsOpen )
	{
		if ( m_renderExitFunc )
		{
			m_renderExitFunc( *this );
		}
		return false;
	}
	m_bIsRendered = true;
	return true;
}

void CImPopupWindow::Close()
{
	m_bIsOpen = false;
}
