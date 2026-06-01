#include "pch.h"
#include "ImWindow.h"

#include "Editor/ImWindow/ImWindowFlag.h"   // IMWINDOW_FLAG_*
#include "Editor/ImGuiUtillity.h"            // ImGui::Utillity::*

CImWindow::CImWindow(ImGuiID id, ImGuiID parentId)
	: m_title("new frame")
	, m_stableID(std::to_string(id))
	, m_hashedID(id)
	, m_ownerID(parentId)
	, m_dockID(0)
	, m_imWindow(nullptr)
	, m_imWndClass({})
	, m_imguiFlags(ImGuiWindowFlags_None)
	, m_windowFlags(IMWINDOW_FLAG_NONE)
	, m_smoothWindowTick(0.0f)
	, m_smoothWindowCount(1.0f)
	, m_startWindowSize({ 0,0 })
	, m_targetWindowSize({ 0,0 })
	, m_startWindowPos({ 0,0 })
	, m_targetWindowPos({ 0,0 })
	, m_bIsFirstTick(true)
	, m_bIsAlive(true)
	, m_bBeginResult(false)
	, m_initDockLayoutDirection(ImGuiDir_None)
	, m_initDockSlotIsSet(false)
	, m_bIsVisible({true, true})
	, m_bIsLock({false, false})
	, m_bIsFocused({false, false})
	, m_bIsClipped({false, false})
	, m_bIsRendered({false, false})
{
}

CImWindow::~CImWindow()
{
}

void CImWindow::Initialize()
{
	HandleCreate();
}
void CImWindow::Finalize()
{
	HandleDestroy();
}
void CImWindow::Update()
{
	HandleUpdate();
}

UINT CImWindow::GetID() const
{
	return m_hashedID;
}

ImGuiID CImWindow::GetOwnerID() const
{
	return m_ownerID;
}

const char* CImWindow::GetTitle() const
{
	return m_title.c_str();
}

void CImWindow::SetTitle(const char* title)
{
	m_title = title ? title : "";
}

void CImWindow::SetLocalizedTitleKey(const char* key)
{
	m_localizedTitleKey = (key && key[0] != '\0') ? key : std::string();
	if (false == m_localizedTitleKey.empty())
	{
		m_title = Loc::Text(m_localizedTitleKey.c_str());
	}
}

void CImWindow::SetStableID(const char* stableID)
{
	m_stableID = (nullptr != stableID && '\0' != stableID[0]) ? stableID : std::to_string(m_hashedID);
}

const char* CImWindow::GetImGuiLabel() const
{
	m_imguiLabel = m_title + "###" + m_stableID;
	return m_imguiLabel.c_str();
}

void CImWindow::SetSize(ImVec2 size, bool delay)
{
	m_startWindowSize = GetSize();
	m_targetWindowSize = size;
	if(false == delay)
	{
		m_smoothWindowTick = 0.0f;
	}
	else
	{
		m_smoothWindowTick = m_smoothWindowCount;
	}
}

ImVec2 CImWindow::GetSize() const
{
	return m_imWindow ? m_imWindow->Size : ImVec2(0,0);
}

void CImWindow::SetPosition( ImVec2 pos , bool delay )
{
	m_startWindowPos = GetPosition();
	m_targetWindowPos = pos;
	if ( false == delay )
	{
		m_smoothWindowTick = 0.0f;
	}
	else
	{
		m_smoothWindowTick = m_smoothWindowCount;
	}
}

ImVec2 CImWindow::GetPosition() const
{
	return m_imWindow ? m_imWindow->Pos : ImVec2(0,0);
}

void CImWindow::SetVisible( bool b )
{
	m_bIsVisible.first = b;
	if (m_bIsFirstTick)
	{
		m_bIsVisible.second = b;
	}
}

bool CImWindow::GetVisible() const
{
	return m_bIsVisible.first;
}

BitFlag& CImWindow::GetImGuiWindowFlags()
{
	return m_imguiFlags;
}

BitFlag& CImWindow::GetCustomWindowFlags()
{
	return m_windowFlags;
}

void CImWindow::Destroy()
{
	if (m_bIsAlive)
	{
		m_bIsAlive = false;
		OnClose();
	}
}

void CImWindow::Focus()
{
	if (m_bIsAlive)
	{
		ImGui::SetWindowFocus(GetImGuiLabel());
	}
}

void CImWindow::InitializeDockLayout( ImGuiDir dir )
{
	m_initDockLayoutDirection = dir;
	m_initDockSlotIsSet = false;
}

void CImWindow::InitializeDockLayout( const char* slot )
{
	m_initDockSlot    = slot ? slot : "";
	m_initDockSlotIsSet = true;
}

ImGuiWindow* CImWindow::GetImGuiWindow()
{
	return m_imWindow;
}

bool CImWindow::IsAlive() const
{
	return m_bIsAlive;
}

ImGuiDir CImWindow::GetInitDockLayout() const
{
	return m_initDockLayoutDirection;
}

void CImWindow::InitializeWindowRect()
{
	if (m_bIsFirstTick)
	{
		m_startWindowSize = m_targetWindowSize * 0.2f;
		ImGui::SetNextWindowSize(m_startWindowSize , ImGuiCond_FirstUseEver);
		
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 nextPos;
		if(m_startWindowPos == ImVec2(0,0))
		{
			ImVec2 center;
			if(0 != m_ownerID)
			{
				if(m_ownerWindow)
				{
					center = m_ownerWindow->GetPosition() + (m_ownerWindow->GetSize() * 0.5f);
				}
				else
				{
					center = viewport->Pos + (viewport->Size * 0.5f);
				}
			}
			else
			{
				center = viewport->Pos + (viewport->Size * 0.5f);
			}
			nextPos = center - (m_targetWindowSize * 0.5f);
		}
		else
		{
			nextPos = viewport->Pos + m_startWindowPos;
		}
		ImGui::SetNextWindowPos(nextPos , ImGuiCond_FirstUseEver);
		SetPosition( nextPos , false );
	}
}

void CImWindow::UpdateWindowState()
{
	m_bIsVisible.second = m_bIsVisible.first;
	m_bIsLock.second = m_bIsLock.first;
	m_bIsFocused.second = m_bIsFocused.first;
	m_bIsClipped.second = m_bIsClipped.first;
	m_bIsRendered.second = m_bIsRendered.first;
}

namespace
{
	float easeInOutBack( float x ) {
		const float c1 = 2.0f;
		const float c2 = c1 * 1.525f;
	
		if ( x < 0.5f ) {
			float t = 2.0f * x;
			return ( t * t * ( ( c2 + 1.0f ) * t - c2 ) ) * 0.5f;
		}
		else {
			float t = 2.0f * x - 2.0f;
			return ( t * t * ( ( c2 + 1.0f ) * t + c2 ) + 2.0f ) * 0.5f;
		}
	}
}

void CImWindow::UpdateWindowRect()
{
	if (false == m_bIsFirstTick && m_imWindow)
	{
		if ( m_smoothWindowTick > 0.0f )
		{
			ImGuiIO io = ImGui::GetIO();

			m_smoothWindowTick -= io.DeltaTime;
			if ( m_smoothWindowTick < 0 )
			{
				m_smoothWindowTick = 0.0f;
				ImGui::SetNextWindowPos( m_targetWindowPos );
				ImGui::SetNextWindowSize( m_targetWindowSize );
			}
			else
			{
				float ratio = 1.0f - ( m_smoothWindowTick / m_smoothWindowCount );
				float lerpT = easeInOutBack( ratio );

				ImVec2 size = ImLerp( m_startWindowSize , m_targetWindowSize , lerpT );
				ImVec2 pos  = m_targetWindowPos + ( m_targetWindowSize * 0.5f ) - ( size * 0.5f );
				ImGui::SetNextWindowPos( pos );
				ImGui::SetNextWindowSize( size );
			}
		}
	}
}
void CImWindow::HandleCreate()
{
	OnCreate();
}

void CImWindow::HandleDestroy()
{
	OnDestroy();
}

void CImWindow::HandleUpdate()
{
	if (m_bIsAlive)
	{
		ImGui::PushID(this);

		// 로컬라이즈 키가 설정된 경우 매 프레임 타이틀 갱신 (언어 전환 즉시 반영)
		if (false == m_localizedTitleKey.empty())
		{
			m_title = Loc::Text(m_localizedTitleKey.c_str());
		}

		InitializeWindowRect();
		UpdateWindowRect();

		OnUpdate();

		if (m_bIsVisible.first && m_bIsVisible.first != m_bIsVisible.second)
		{
			OnShow();
			OnOpen();
		}
		else if (false == m_bIsVisible.first && m_bIsVisible.first != m_bIsVisible.second)
		{
			HandleHidden();
			OnHide();
		}
		if (m_bIsVisible.first)
		{
			HandleBegin();

			const bool isLock = m_bIsLock.first;
			if (isLock)
			{
				ImGui::BeginDisabled();
			}

			HandleFocus();
			HandleRender();
			HandleEnd();

			if (isLock)
			{
				ImGui::EndDisabled();
			}

			if (false == m_bIsAlive)
			{
				OnClose();
			}
		}
		UpdateWindowState();
		ImGui::PopID();

		m_bIsFirstTick = false;
	}
}

void CImWindow::HandleBegin()
{
	OnPreBegin();

	bool*		isAlive		= m_windowFlags.Has(IMWINDOW_FLAG_NO_CLOSE_BUTTON) ? nullptr : &m_bIsAlive;
	ImGuiWindowFlags flags	= static_cast<ImGuiWindowFlags>(m_imguiFlags.Get() | ImGuiWindowFlags_NoCollapse);

	if(m_ownerWindow)
	{
		ImGui::SetNextWindowClass(&m_ownerWindow->m_imWndClass);
	}
	m_bBeginResult = ImGui::Begin(GetImGuiLabel(), isAlive, flags);

	m_imWindow = ImGui::GetCurrentWindow();

	OnPostBegin();

	if (m_bBeginResult && m_imguiFlags.Has(ImGuiWindowFlags_MenuBar) && ImGui::BeginMenuBar())
	{
		OnMenuBar();
		ImGui::EndMenuBar();
	}
}

void CImWindow::HandleRender()
{
	if (m_bBeginResult && ImGui::Utillity::IsWindowDrawable())
	{
		if (m_bIsClipped.first)
		{
			m_bIsClipped.first = false;
			OnClipExit();
		}
		if (false == m_bIsRendered.first)
		{
			m_bIsRendered.first = true;
			OnRenderEnter();
		}
		OnRenderStay();
	}
	else
	{
		if (m_bIsRendered.first)
		{
			m_bIsRendered.first = false;
			OnRenderExit();
		}
		if (false == m_bIsClipped.first)
		{
			m_bIsClipped.first = true;
			OnClipEnter();
		}
		OnClipStay();
	}
}

void CImWindow::HandleEnd()
{
	OnPreEnd();
	ImGui::End();
	OnPostEnd();
}

void CImWindow::HandleFocus()
{
	if (false == m_bIsFirstTick)
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
		{
			if (false == m_bIsFocused.first)
			{
				m_bIsFocused.first = true;
				OnFocusEnter();
			}
			OnFocusStay();
		}
		else
		{
			if (m_bIsFocused.first)
			{
				m_bIsFocused.first = false;
				OnFocusExit();
			}
		}
	}
}

void CImWindow::HandleEvent()
{
	
}

void CImWindow::HandleHidden()
{
	if (m_bIsRendered.first)
	{
		m_bIsRendered.first = false;
		OnRenderExit();
	}
	if (m_bIsFocused.first)
	{
		m_bIsFocused.first = false;
		OnFocusExit();
	}
	if (m_bIsClipped.first)
	{
		m_bIsClipped.first = false;
		OnClipExit();
	}
}
