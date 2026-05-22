#include "pch.h"
#include "MainDockWindow.h"

void CMainDockWindow::OnCreate()
{
    m_imWndClass.ClassId = ImHashStr("MainDockID");
    m_imWndClass.DockingAllowUnclassed = false;
    m_imWndClass.DockingAlwaysTabBar = true;

    m_imguiFlags = ImGuiWindowFlags_MenuBar;
    m_windowFlags = IMWINDOW_FLAG_NO_CLOSE_BUTTON;
    m_imguiDockFlags = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;
	SetDockLayout(ImGuiDir_Left, 0.22f);
	SetDockLayout(ImGuiDir_Right, 0.28f);

    SetTitle("Main");

	ImGuiID id = GetID();
	Editor::Hierarchy = Editor::ImEditor->CreateImWindow<CHierarchyTool>("Hierarchy", id);
	Editor::SceneView = Editor::ImEditor->CreateImWindow<CSceneViewTool>("SceneView", id);
	Editor::Inspector = Editor::ImEditor->CreateImWindow<CInspectorTool>("Inspector", id);
	Editor::AssetBrowser = Editor::ImEditor->CreateImWindow<CAssetBrowserTool>("AssetBrowser", id);
	if (Editor::Inspector)
	{
		Editor::Inspector->InitializeDockLayout(ImGuiDir_Right);
	}
	if (Editor::AssetBrowser)
	{
		Editor::AssetBrowser->InitializeDockLayout(ImGuiDir_Down);
	}
	if (Editor::Hierarchy)
	{
		Editor::Hierarchy->InitializeDockLayout(ImGuiDir_Left);
	}
	if (Editor::SceneView)
	{
		Editor::SceneView->InitializeDockLayout(ImGuiDir_None);
	}
}

void CMainDockWindow::OnMenuBar()
{
	if (ImGui::BeginMenu(Utillity::U8(u8"씬")))
	{
		if (ImGui::MenuItem(Utillity::U8(u8"새로운 씬 만들기")))
		{
			File::Path projectPath;
			if (File::ShowOpenFileDialog(
				nullptr,
				L"프로젝트 열기",
				L"",
				{ { L"JBro Project", L"*.Jproject" }, { L"All Files", L"*.*" } },
				projectPath))
			{
				SafePtr<CProjectManager> projectManager = Editor::ImEditor->GetProjectManager() : nullptr;
				if (projectManager)
				{
					ProjectLoadDesc desc;
					desc.ProjectFilePath = projectPath;
					projectManager->LoadProject(desc);
				}
			}
		}
		ImGui::EndMenu();
	}
}
