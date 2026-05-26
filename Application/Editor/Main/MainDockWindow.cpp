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

	// ─── 레이아웃 정의 (등록 순서대로 적용) ────────────────────────────────
	//
	//  ┌──────────────────────────────┬───────────────┐
	//  │                              │               │
	//  │  SceneView | GameView (탭)   │  Hierarchy    │
	//  │                              │               │
	//  │                              ├───────────────┤
	//  │                              │               │
	//  ├──────────────────────────────┤  Inspector    │
	//  │  AssetBrowser | Log (탭)     │               │
	//  └──────────────────────────────┴───────────────┘
	//
	// Step 1: 루트("")를 Right(25%) 방향으로 분할
	//         → "Right" = 오른쪽 패널, "" = 나머지(중앙+하단)
	AddDockSplit("",      ImGuiDir_Right, 0.25f, "Right");

	// Step 2: 중앙("")을 Down(22%) 방향으로 분할
	//         → "Bottom" = 하단 스트립, "" = 나머지(SceneView 영역)
	AddDockSplit("",      ImGuiDir_Down,  0.35f, "Bottom");

	// Step 3: 오른쪽("Right")을 Down(50%) 방향으로 분할
	//         → "RightBottom" = Inspector, "Right" = Hierarchy
	AddDockSplit("Right", ImGuiDir_Down,  0.50f, "RightBottom");

    SetTitle("Main");

	ImGuiID id = GetID();
	Editor::Hierarchy    = Editor::ImEditor->CreateImWindow<CHierarchyTool>   ("Hierarchy",    id);
	Editor::SceneView    = Editor::ImEditor->CreateImWindow<CSceneViewTool>   ("SceneView",    id);
	Editor::GameView     = Editor::ImEditor->CreateImWindow<CGameViewTool>    ("GameView",     id);
	Editor::Inspector    = Editor::ImEditor->CreateImWindow<CInspectorTool>   ("Inspector",    id);
	Editor::AssetBrowser = Editor::ImEditor->CreateImWindow<CAssetBrowserTool>("AssetBrowser", id);
	Editor::LogTool      = Editor::ImEditor->CreateImWindow<CLogTool>         ("Log",          id);

	// 슬롯 이름으로 창 배치
	if (Editor::SceneView)
	{
		Editor::SceneView->InitializeDockLayout("");        // 중앙 영역
	}
	if (Editor::GameView)
	{
		Editor::GameView->InitializeDockLayout("");         // SceneView와 탭으로 합침
	}
	if (Editor::AssetBrowser)
	{
		Editor::AssetBrowser->InitializeDockLayout("Bottom");
	}
	if (Editor::LogTool)
	{
		Editor::LogTool->InitializeDockLayout("Bottom");    // AssetBrowser와 탭으로 합침
	}
	if (Editor::Hierarchy)
	{
		Editor::Hierarchy->InitializeDockLayout("Right");   // Step3 후 나머지
	}
	if (Editor::Inspector)
	{
		Editor::Inspector->InitializeDockLayout("RightBottom");
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
