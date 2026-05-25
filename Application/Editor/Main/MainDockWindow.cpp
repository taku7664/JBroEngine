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
	SetDockLayout(ImGuiDir_Right, 0.25f);
	SetDockLayout(ImGuiDir_Down, 0.70f);
	SetDockLayout(ImGuiDir_Left, 0.20f);
	SetDockLayout(ImGuiDir_Up, 0.50f);

    SetTitle("Main");

	ImGuiID id = GetID();
	Editor::Hierarchy    = Editor::ImEditor->CreateImWindow<CHierarchyTool>   ("Hierarchy",    id);
	Editor::SceneView    = Editor::ImEditor->CreateImWindow<CSceneViewTool>   ("SceneView",    id);
	Editor::GameView     = Editor::ImEditor->CreateImWindow<CGameViewTool>    ("GameView",     id);
	Editor::Inspector    = Editor::ImEditor->CreateImWindow<CInspectorTool>   ("Inspector",    id);
	Editor::AssetBrowser = Editor::ImEditor->CreateImWindow<CAssetBrowserTool>("AssetBrowser", id);
	Editor::LogTool      = Editor::ImEditor->CreateImWindow<CLogTool>         ("Log",          id);

	if (Editor::Hierarchy)
	{
		Editor::Hierarchy->InitializeDockLayout(ImGuiDir_Left);
	}
	if (Editor::Inspector)
	{
		Editor::Inspector->InitializeDockLayout(ImGuiDir_Right);
	}
	if (Editor::SceneView)
	{
		Editor::SceneView->InitializeDockLayout(ImGuiDir_None);
	}
	if (Editor::GameView)
	{
		// GameView docks next to SceneView (same center area — becomes a tab).
		Editor::GameView->InitializeDockLayout(ImGuiDir_None);
	}
	if (Editor::AssetBrowser)
	{
		Editor::AssetBrowser->InitializeDockLayout(ImGuiDir_Down);
	}
	if (Editor::LogTool)
	{
		Editor::LogTool->InitializeDockLayout(ImGuiDir_Down);
	}
}

void CMainDockWindow::OnMenuBar()
{
	if (ImGui::BeginMenu(Utillity::U8(u8"시뮬레이션")))
	{
		SafePtr<CSceneManager> sceneManager = Core::SceneManager;
		const bool canUseSimulation = sceneManager.IsValid();
		const bool isPlaying = canUseSimulation && sceneManager->IsSimulationPlaying();
		const bool isPaused = canUseSimulation && sceneManager->IsSimulationPaused();

		if (false == canUseSimulation || isPlaying)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem(Utillity::U8(u8"실행")))
		{
			sceneManager->PlaySimulation();
			if (Editor::GameView)
			{
				Editor::GameView->Focus();
			}
		}
		if (false == canUseSimulation || isPlaying)
		{
			ImGui::EndDisabled();
		}

		if (false == canUseSimulation || false == isPlaying)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem(Utillity::U8(u8"일시정지")))
		{
			sceneManager->PauseSimulation();
		}
		if (false == canUseSimulation || false == isPlaying)
		{
			ImGui::EndDisabled();
		}

		if (false == canUseSimulation || (false == isPlaying && false == isPaused))
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem(Utillity::U8(u8"중단")))
		{
			sceneManager->StopSimulation();
		}
		if (false == canUseSimulation || (false == isPlaying && false == isPaused))
		{
			ImGui::EndDisabled();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Utillity::U8(u8"편집")))
	{
		if (false == Editor::CommandManager.CanUndo())
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Undo", "Ctrl+Z"))
		{
			Editor::CommandManager.Undo();
		}
		if (false == Editor::CommandManager.CanUndo())
		{
			ImGui::EndDisabled();
		}

		if (false == Editor::CommandManager.CanRedo())
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y"))
		{
			Editor::CommandManager.Redo();
		}
		if (false == Editor::CommandManager.CanRedo())
		{
			ImGui::EndDisabled();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Utillity::U8(u8"씬")))
	{
		if (ImGui::MenuItem(Utillity::U8(u8"새로운 씬 만들기")))
		{
		}
		ImGui::EndMenu();
	}
}
