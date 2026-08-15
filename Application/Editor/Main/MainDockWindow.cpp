#include "pch.h"
#include "MainDockWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"

#include "Editor/Editor.h"
#include "Editor/Main/AssetBrowser/AssetBrowserTool.h"
#include "Editor/Main/GameView/GameViewTool.h"
#include "Editor/Main/Layers/LayerTool.h"
#include "Editor/Main/Inspector/InspectorTool.h"
#include "Editor/Main/Log/LogTool.h"
#include "Editor/Main/CanvasView/CanvasViewTool.h"
#include "Editor/Main/ShortcutReference/ShortcutReferenceTool.h"
#include "Editor/Main/Debug/GpuProfilerWindow.h"
#include "Editor/Main/Debug/CpuProfilerWindow.h"
#include "Editor/Main/Importer/SpriteImporterWindow.h"
#include "Editor/Main/Importer/AudioImporterWindow.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Canvas/CanvasManager.h"
#include "Utillity/File/FileUtillities.h"

void CMainDockWindow::OnCreate()
{
    m_imWndClass.ClassId = ImHashStr("MainDockID");
    m_imWndClass.DockingAllowUnclassed = false;
    m_imWndClass.DockingAlwaysTabBar = true;

    m_imguiFlags = ImGuiWindowFlags_MenuBar;
    // 메인 독은 닫을 수 없다 — 닫으면 에디터에 남는 게 없다.
    // (노드 버튼 끄기는 CImDockWindow 기본값이라 여기서 다시 적지 않는다.)
    m_windowFlags = IMWINDOW_FLAG_NO_CLOSE_BUTTON;

	// ─── 레이아웃 정의 (등록 순서대로 적용) ────────────────────────────────
	//
	//  ┌──────────────────────────────┬───────────────┐
	//  │                              │               │
	//  │  CanvasView | GameView (탭)  │  Layers       │
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
	//         → "Bottom" = 하단 스트립, "" = 나머지(CanvasView 영역)
	AddDockSplit("",      ImGuiDir_Down,  0.35f, "Bottom");

	// Step 3: 오른쪽("Right")을 Down(50%) 방향으로 분할
	//         → "RightBottom" = Inspector, "Right" = Layers
	AddDockSplit("Right", ImGuiDir_Down,  0.50f, "RightBottom");

    SetLocalizedTitleKey(EditorLocKeys::WindowMain);

	ImGuiID id = GetID();
	// 안정 ID = ImGui 도킹 키다. 여기를 바꾸면 이미 저장된 창 배치(.jproject 의 ImGuiIniSettings)가
	// 그 창을 못 찾아 기본 배치로 한 번 돌아간다 — 이름을 끝까지 맞추는 값으로 감수한다.
	Editor::Layers    = Editor::ImEditor->CreateImWindow<CLayerTool>   ("Layers",       id);
	Editor::CanvasView    = Editor::ImEditor->CreateImWindow<CCanvasViewTool>   ("CanvasView",   id);
	Editor::GameView     = Editor::ImEditor->CreateImWindow<CGameViewTool>    ("GameView",     id);
	Editor::Inspector    = Editor::ImEditor->CreateImWindow<CInspectorTool>   ("Inspector",    id);
	Editor::AssetBrowser = Editor::ImEditor->CreateImWindow<CAssetBrowserTool>("AssetBrowser", id);
	Editor::LogTool      = Editor::ImEditor->CreateImWindow<CLogTool>         ("Log",          id);
	Editor::ShortcutReference = Editor::ImEditor->CreateImWindow<CShortcutReferenceTool>("ShortcutReference", id);
	Editor::GpuProfiler       = Editor::ImEditor->CreateImWindow<CGpuProfilerWindow>("GpuProfiler", id);
	Editor::CpuProfiler       = Editor::ImEditor->CreateImWindow<CCpuProfilerWindow>("CpuProfiler", id);

	// 슬롯 이름으로 창 배치
	if (Editor::CanvasView)
	{
		Editor::CanvasView->InitializeDockLayout("");        // 중앙 영역
	}
	if (Editor::GameView)
	{
		Editor::GameView->InitializeDockLayout("");         // CanvasView와 탭으로 합침
	}
	if (Editor::AssetBrowser)
	{
		Editor::AssetBrowser->InitializeDockLayout("Bottom");
	}
	if (Editor::LogTool)
	{
		Editor::LogTool->InitializeDockLayout("Bottom");    // AssetBrowser와 탭으로 합침
	}
	if (Editor::ShortcutReference)
	{
		Editor::ShortcutReference->InitializeDockLayout("Bottom");
		Editor::ShortcutReference->SetVisible(false);
	}
	if (Editor::GpuProfiler)
	{
		Editor::GpuProfiler->InitializeDockLayout("Bottom");
		Editor::GpuProfiler->SetVisible(false);
	}
	if (Editor::CpuProfiler)
	{
		Editor::CpuProfiler->InitializeDockLayout("Bottom");
		Editor::CpuProfiler->SetVisible(false);
	}
	if (Editor::Layers)
	{
		Editor::Layers->InitializeDockLayout("Right");   // Step3 후 나머지
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
	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuSimulation)))
	{
		const bool canTogglePlay = Editor::ShortcutManager.CanExecute(EEditorShortcut::TogglePlay);
		if (false == canTogglePlay)
		{
			ImGui::BeginDisabled();
		}
		const std::string playShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::TogglePlay);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuSimulationPlayToggle), playShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::TogglePlay);
		}
		if (false == canTogglePlay)
		{
			ImGui::EndDisabled();
		}

		const bool canTogglePause = Editor::ShortcutManager.CanExecute(EEditorShortcut::TogglePause);
		if (false == canTogglePause)
		{
			ImGui::BeginDisabled();
		}
		const std::string pauseShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::TogglePause);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuSimulationPauseToggle), pauseShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::TogglePause);
		}
		if (false == canTogglePause)
		{
			ImGui::EndDisabled();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuEdit)))
	{
		const bool canUndo = Editor::ShortcutManager.CanExecute(EEditorShortcut::Undo);
		if (false == canUndo)
		{
			ImGui::BeginDisabled();
		}
		const std::string undoShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::Undo);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuEditUndo), undoShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::Undo);
		}
		if (false == canUndo)
		{
			ImGui::EndDisabled();
		}

		const bool canRedo = Editor::ShortcutManager.CanExecute(EEditorShortcut::Redo);
		if (false == canRedo)
		{
			ImGui::BeginDisabled();
		}
		const std::string redoShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::Redo);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuEditRedo), redoShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::Redo);
		}
		if (false == canRedo)
		{
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		const bool canCopy = Editor::ShortcutManager.CanExecute(EEditorShortcut::CopyObjects);
		if (false == canCopy) ImGui::BeginDisabled();
		const std::string copyShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::CopyObjects);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::EditorMenuCopyObject), copyShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::CopyObjects);
		}
		if (false == canCopy) ImGui::EndDisabled();

		const bool canPaste = Editor::ShortcutManager.CanExecute(EEditorShortcut::PasteObjects);
		if (false == canPaste) ImGui::BeginDisabled();
		const std::string pasteShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::PasteObjects);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::EditorMenuPasteObject), pasteShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::PasteObjects);
		}
		if (false == canPaste) ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	// ── "창" 메뉴 — "에디터" + "임포터" 서브메뉴 ──────────────────────────
	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuWindow)))
	{
		// 에디터 — 도킹된 자식 윈도우(인스펙터/캔버스뷰/하이라키 등) 토글
		if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuWindowEditor)))
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
		if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuWindowImporter)))
		{
			if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuImporterSprite)))
			{
				if (Editor::SpriteImporter) Editor::SpriteImporter->SetVisible(true);
			}
			if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuImporterAudio)))
			{
				if (Editor::AudioImporter) Editor::AudioImporter->SetVisible(true);
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}
}
