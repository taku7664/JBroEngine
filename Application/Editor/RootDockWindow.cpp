#include "pch.h"
#include "RootDockWindow.h"

#include "Editor/Editor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

void CRootDockWindow::OnCreate()
{
    m_imWndClass.ClassId = ImHashStr("RootDockID");
    m_imWndClass.DockingAllowUnclassed = false;
    m_imWndClass.DockingAlwaysTabBar = false;

    m_imguiFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;

    m_imguiDockFlags =
        ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;

    m_customDockFlags =
        IMDOCKWINDOW_FLAG_FULLSCREEN | IMDOCKWINDOW_FLAG_PADDING;

    SetTitle("Root");
}

void CRootDockWindow::OnMenuBar()
{
	if (ImGui::BeginMenu(Utillity::U8(u8"파일")))
	{
		if (ImGui::MenuItem(Utillity::U8(u8"프로젝트 열기")))
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

