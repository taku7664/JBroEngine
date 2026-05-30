#include "pch.h"
#include "MainDockWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"

#include "Editor/Editor.h"
#include "Editor/Main/AssetBrowser/AssetBrowserTool.h"
#include "Editor/Main/GameView/GameViewTool.h"
#include "Editor/Main/Hierarchy/HierarchyTool.h"
#include "Editor/Main/Inspector/InspectorTool.h"
#include "Editor/Main/Log/LogTool.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Editor/Main/Importer/SpriteImporterWindow.h"
#include "Editor/Main/Importer/AudioImporterWindow.h"
#include "Engine/Core/Core.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/GameFramework/Scene/SceneManager.h"

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

    SetLocalizedTitleKey("window.main");

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

	// ── 임포터 윈도우 — 도킹 안 하고 자유 떠 다니는 다이얼로그.
	// 메뉴바 "임포터" 에서 SetVisible(true) 로 띄운다.
	Editor::SpriteImporter = Editor::ImEditor->CreateImWindow<CSpriteImporterWindow>("SpriteImporter", 0);
	Editor::AudioImporter  = Editor::ImEditor->CreateImWindow<CAudioImporterWindow> ("AudioImporter",  0);
}

void CMainDockWindow::OnMenuBar()
{
	// ShowDemoWindow 안에 있는 ImGui::ShowExampleAppAssetsBrowser()를 참고해서 에셋 브라우저 드로우를 개선.
	// 드래깅이나 다중 선택을 통해 다중 선택이 가능해야함
	ImGui::ShowDemoWindow();

	if (ImGui::BeginMenu(Loc::Text("menu.simulation")))
	{
		SafePtr<CSceneManager> sceneManager = Core::SceneManager;
		const bool canUseSimulation = sceneManager.IsValid();
		const bool isPlaying = canUseSimulation && sceneManager->IsSimulationPlaying();
		const bool isPaused = canUseSimulation && sceneManager->IsSimulationPaused();

		if (false == canUseSimulation || isPlaying)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem(Loc::Text("menu.simulation.play")))
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
		if (ImGui::MenuItem(Loc::Text("menu.simulation.pause")))
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
		if (ImGui::MenuItem(Loc::Text("menu.simulation.stop")))
		{
			sceneManager->StopSimulation();
		}
		if (false == canUseSimulation || (false == isPlaying && false == isPaused))
		{
			ImGui::EndDisabled();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Loc::Text("menu.edit")))
	{
		if (false == Editor::CommandManager.CanUndo())
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem(Loc::Text("menu.edit.undo"), "Ctrl+Z"))
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
		if (ImGui::MenuItem(Loc::Text("menu.edit.redo"), "Ctrl+Y"))
		{
			Editor::CommandManager.Redo();
		}
		if (false == Editor::CommandManager.CanRedo())
		{
			ImGui::EndDisabled();
		}
		ImGui::EndMenu();
	}

	// ── "창" 메뉴 — "에디터" + "임포터" 서브메뉴 ──────────────────────────
	if (ImGui::BeginMenu(Loc::Text("menu.window")))
	{
		// 에디터 — 도킹된 자식 윈도우(인스펙터/씬뷰/하이라키 등) 토글
		if (ImGui::BeginMenu(Loc::Text("menu.window.editor")))
		{
			for (SafePtr<CImWindow>& child : m_childImWindowVector)
			{
				if (child.IsValid())
				{
					const char* title = child->GetTitle();
					bool isVisible = child->GetVisible();
					if (ImGui::MenuItem(title, nullptr, isVisible))
					{
						child->SetVisible(!isVisible);
					}
				}
			}
			ImGui::EndMenu();
		}

		// 임포터 — 자산 임포트 다이얼로그 윈도우
		if (ImGui::BeginMenu(Loc::Text("menu.window.importer")))
		{
			if (ImGui::MenuItem(Loc::Text("menu.importer.sprite")))
			{
				if (Editor::SpriteImporter) Editor::SpriteImporter->SetVisible(true);
			}
			if (ImGui::MenuItem(Loc::Text("menu.importer.audio")))
			{
				if (Editor::AudioImporter) Editor::AudioImporter->SetVisible(true);
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}
}
