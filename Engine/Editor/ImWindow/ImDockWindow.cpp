#include "pch.h"
#include "ImDockWindow.h"

CImDockWindow::CImDockWindow(ImGuiID id, ImGuiID parentId)
	: CImWindow(id, parentId)
	, m_mainDockID(0)
	, m_mainSplitedID(0)
	, m_splitedID({ 0 })
	, m_splitRatio({ 0.0f })
	, m_bNeedRebuildDockLayout(true)
{
	m_mainDockID = ImHashStr("DockSpace", 0, GetID());
	m_imWndClass.ClassId = m_mainDockID;
	m_imWndClass.DockingAllowUnclassed = false;
	m_imWndClass.DockingAlwaysTabBar = false;
}

CImDockWindow::~CImDockWindow()
{
	for(auto& childWnd : m_childImWindowVector)
	{
		if (childWnd)
		{
			childWnd->Destroy();
		}
	}
}

void CImDockWindow::SetDockLayout(ImGuiDir dir, float splitRatio)
{
	m_splitRatio[dir] = splitRatio;
}

BitFlag& CImDockWindow::GetImGuiDockFlags()
{
	return m_imguiDockFlags;
}

BitFlag& CImDockWindow::GetCustomDockFlags()
{
	return m_customDockFlags;
}

bool CImDockWindow::AddChildImWindow(SafePtr<IImWindow> child)
{
	if (false == child.IsValid())
	{
		return false;
	}
	if (child->GetID() == GetID())
	{
		return false;
	}
	if(false == HasChildImWindow(child->GetID()))
	{
		SafePtr<CImWindow> castedChild = DynamicSafePtrCast<CImWindow>(child);
		if (false == castedChild.IsValid())
		{
			return false;
		}

		m_childImWindowVector.push_back(castedChild);
		castedChild->m_ownerID = GetID();
		castedChild->m_ownerWindow = SafeFromThis();
		m_bNeedRebuildDockLayout = true;
		return true;
	}
	return false;
}

void CImDockWindow::RemoveChildImWindow(ImGuiID id)
{
	for (SafePtr<CImWindow>& childWnd : m_childImWindowVector)
	{
		if (childWnd && childWnd->GetID() == id)
		{
			childWnd->m_ownerID = 0;
			childWnd->m_ownerWindow.Reset();
			break;
		}
	}

	m_childImWindowVector.erase( std::remove_if(
		m_childImWindowVector.begin(),
		m_childImWindowVector.end(),
		[id] (const SafePtr<CImWindow>& a) {
			return false == a.IsValid() || a->GetID() == id;
		}),
		m_childImWindowVector.end()
	);
	m_bNeedRebuildDockLayout = true;
}

SafePtr<CImWindow> CImDockWindow::FindChildImWindow(ImGuiID id)
{
	for (const auto& pWnd : m_childImWindowVector)
	{
		if(pWnd && pWnd->GetID() == id)
		{
			return pWnd;
		}
	}
	return nullptr;
}

bool CImDockWindow::HasChildImWindow(ImGuiID id) const
{
	for (const auto& pWnd : m_childImWindowVector)
	{
		if(pWnd && pWnd->GetID() == id)
		{
			return true;
		}
	}
	return false;
}

void CImDockWindow::OnPreBegin()
{
	PushDockStyle();
}

void CImDockWindow::OnPostBegin()
{
	bool isBeginDockBuild = false;
	isBeginDockBuild = BeginBuildDockLayout();

	SubmitDockSpace();
	PopDockStyle();

	for(std::size_t i = 0; i < m_childImWindowVector.size(); ++i)
	{
		if(SafePtr<CImWindow>& childWnd = m_childImWindowVector[i])
		{
			if(isBeginDockBuild)
			{
				const char* label = childWnd->GetTitle();
				const ImGuiDir dockDir = childWnd->m_initDockLayoutDirection;
				ImGuiID splitID = (dockDir >= 0 && dockDir < ImGuiDir_COUNT) ? m_splitedID[dockDir] : m_mainSplitedID;
				if (0 == splitID)
				{
					splitID = m_mainSplitedID;
				}
				ImGui::DockBuilderDockWindow(label , splitID);
			}
			childWnd->Update();
		}
	}

	if (isBeginDockBuild)
	{
		EndBuildDockLayout();
	}
}

void CImDockWindow::OnPostEnd()
{
}

void CImDockWindow::SubmitDockSpace()
{
	//////////////////////////////////////////
	// Sumit the DockSpace
	//////////////////////////////////////////
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	float       minWinSizeX = style.WindowMinSize.x;
	style.WindowMinSize.x = 370.0f;
	if ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
	{
		ImGui::DockSpace(m_mainDockID , ImVec2(0.0f , 0.0f) , m_imguiDockFlags.operator int(), &m_imWndClass);
	}
	style.WindowMinSize.x = minWinSizeX;
}

bool CImDockWindow::BeginBuildDockLayout()
{
	if (true == m_bNeedRebuildDockLayout)
    {
		m_bNeedRebuildDockLayout = false;
        ImGui::DockBuilderRemoveNode(m_mainDockID);
        ImGui::DockBuilderAddNode(m_mainDockID, ImGuiDockNodeFlags_DockSpace | m_imguiDockFlags.Get());
		if (m_imWindow)
		{
			ImGui::DockBuilderSetNodeSize(m_mainDockID, m_imWindow->Size);
		}
        ImGuiID mainId = m_mainDockID;

		for(int i = 0; i < ImGuiDir_COUNT; ++i)
		{
			if(m_splitRatio[i] > 0.0f)
			{
				ImGuiID id = ImGui::DockBuilderSplitNode(
					mainId,
					(ImGuiDir)i,
					m_splitRatio[i],
					NULL,
					&mainId
				);
				m_splitedID[i] = id;
			}
		}
		m_mainSplitedID = mainId;
        return true;
    }
    return false;
}

void CImDockWindow::EndBuildDockLayout() const
{
	ImGui::DockBuilderFinish(m_mainDockID);
}

void CImDockWindow::PushDockStyle()
{
	if (m_customDockFlags.Has(IMDOCKWINDOW_FLAG_FULLSCREEN))
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos( viewport->Pos );
		ImGui::SetNextWindowSize( viewport->Size );
		ImGui::SetNextWindowViewport( viewport->ID );
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowRounding , 0.0f);
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowBorderSize , 0.0f);
	}
	if (m_customDockFlags.Has(IMDOCKWINDOW_FLAG_PADDING))
	{
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f , 0.0f));
	}
}

void CImDockWindow::PopDockStyle()
{
	m_dockStyleBuilder.PopStyle();
}
