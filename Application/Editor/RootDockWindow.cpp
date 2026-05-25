#include "pch.h"
#include "RootDockWindow.h"

#include "Editor/Editor.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#include "Editor/Main/SceneViewTool.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineContext.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

namespace
{
	void TryLoadLastScene(SafePtr<CProjectManager> pm)
	{
		if (false == pm.IsValid() || false == pm->IsProjectLoaded())
		{
			return;
		}

		const std::string& lastScenePath = pm->GetLastOpenedScenePath();
		if (lastScenePath.empty())
		{
			return;
		}

		if (false == Core::SceneManager.IsValid())
		{
			return;
		}

		const std::filesystem::path absolutePath = pm->GetAssetPath() / lastScenePath;
		std::error_code ec;
		if (false == std::filesystem::exists(absolutePath, ec))
		{
			CSystemLog::Warning("Last scene file not found, skipping auto-load.");
			return;
		}

		CScene* scene = Core::SceneManager->CreateScene(lastScenePath.c_str());
		if (nullptr == scene)
		{
			CSystemLog::Error("Failed to create scene for last opened scene.");
			return;
		}

		CSceneSerializer serializer;
		const std::string absolutePathStr = absolutePath.string();
		if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, absolutePathStr.c_str()))
		{
			if (const EngineContext* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineContext() : nullptr)
			{
				CSpriteRenderSystem* spriteSystem = scene->FindSystem<CSpriteRenderSystem>();
				if (nullptr == spriteSystem)
				{
					spriteSystem = scene->AddSystem<CSpriteRenderSystem>(context->RenderScene.TryGet());
				}
				if (nullptr != spriteSystem)
				{
					spriteSystem->SetRenderScene(context->RenderScene.TryGet());
					spriteSystem->SetDependencies(
						context->AssetManager.TryGet(),
						context->RHIDevice.TryGet(),
						context->Renderer.TryGet());
				}
			}
			Core::SceneManager->SetActiveScene(lastScenePath.c_str());
			Editor::SetActiveScenePath(File::Path(absolutePath));
			Editor::CommandManager.SetActiveDocument(lastScenePath.c_str());
			Editor::CommandManager.MarkSaved(lastScenePath.c_str());
			CSystemLog::Info("Last scene auto-loaded.");
		}
		else
		{
			CSystemLog::Warning("Failed to auto-load last scene.");
		}
	}

	void SaveCurrentEditorState()
	{
		// ── 씬 저장 ─────────────────────────────────────────────────────────────
		bool sceneSaved = false;
		if (Core::SceneManager.IsValid())
		{
			SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
			if (scene.IsValid() && false == Editor::GetActiveScenePath().empty())
			{
				CSceneSerializer serializer;
				const std::string scenePath = Editor::GetActiveScenePath().string();
				if (ESceneSerializeResult::Success == serializer.SaveToFile(*scene, scenePath.c_str()))
				{
					Editor::CommandManager.MarkSaved();
					sceneSaved = true;
					CSystemLog::Info("Scene saved.");
				}
				else
				{
					CSystemLog::Error("Scene save failed.");
				}
			}
			else
			{
				CSystemLog::Warning("Scene save skipped because no scene file is active.");
			}
		}
		else
		{
			CSystemLog::Warning("Scene save skipped because SceneManager is not available.");
		}

		// ── 프로젝트 파일 저장 (씬뷰 카메라 + 프로젝트 설정) ────────────────────
		SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
		if (pm && pm->IsProjectLoaded())
		{
			// 씬뷰 카메라 현재 상태를 ProjectManager에 기록
			if (Editor::SceneView)
			{
				const Vector2<float> camPos  = Editor::SceneView->GetEditorCameraPos();
				const float          camSize = Editor::SceneView->GetEditorCameraSize();
				pm->SetSceneViewCamera(camPos.x, camPos.y, camSize);
			}

			// 마지막으로 열었던 씬 경로 기록 (Assets 기준 상대경로)
			const File::Path& activeScenePath = Editor::GetActiveScenePath();
			if (false == activeScenePath.empty() && false == pm->GetAssetPath().empty())
			{
				std::error_code ec;
				std::filesystem::path relPath = std::filesystem::relative(
					activeScenePath,
					pm->GetAssetPath(),
					ec);
				if (false == static_cast<bool>(ec) && false == relPath.empty())
				{
					pm->SetLastOpenedScenePath(relPath.generic_string());
				}
			}

			if (pm->SaveProject())
			{
				CSystemLog::Info("Project saved.");
			}
			else
			{
				CSystemLog::Error("Project save failed.");
			}
		}

		(void)sceneSaved;
	}
}

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
		IMDOCKWINDOW_FLAG_NO_DOCKING | IMDOCKWINDOW_FLAG_FULLSCREEN | IMDOCKWINDOW_FLAG_PADDING;

    SetTitle("Root");

    // 프로젝트 세팅 창 (플로팅, 처음에는 숨김)
    if (Editor::ImEditor)
    {
        Editor::ProjectSettings =
            Editor::ImEditor->CreateImWindow<CProjectSettingsWindow>("ProjectSettings", 0);
    }
}

void CRootDockWindow::OnRenderStay()
{
	SafePtr<CProjectManager> projectManager = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;

	if (projectManager && false == projectManager->IsProjectLoaded())
	{
		ImText(Utillity::U8(u8"프로젝트를 열려면, 상단 메뉴에서 [파일] > [프로젝트 열기]를 선택하세요."), ImText::Align::Center);
	}
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
				SafePtr<CProjectManager> projectManager = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
				if (projectManager)
				{
					ProjectLoadDesc desc;
					desc.ProjectFilePath = projectPath;
					if (Editor::ImEditor && projectManager->LoadProject(desc))
					{
						m_customDockFlags -= IMDOCKWINDOW_FLAG_NO_DOCKING;
						Editor::MainDockWindow = Editor::ImEditor->CreateImWindow<CMainDockWindow>("MainDockWindow");
						if (Editor::RootDockWindow && Editor::MainDockWindow)
						{
							Editor::RootDockWindow->AddChildImWindow(Editor::MainDockWindow);
						}

						// 프로젝트에 저장된 씬뷰 카메라 위치 복원
						if (Editor::SceneView)
						{
							Editor::SceneView->SetEditorCamera(
								projectManager->GetSceneViewCamX(),
								projectManager->GetSceneViewCamY(),
								projectManager->GetSceneViewCamSize());
						}

						// 마지막으로 열었던 씬 자동 로드
						TryLoadLastScene(projectManager);
					}
				}
			}
		}
		if (ImGui::MenuItem(Utillity::U8(u8"프로젝트 저장")))
		{
			SaveCurrentEditorState();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Utillity::U8(u8"설정")))
	{
		if (ImGui::MenuItem(Utillity::U8(u8"프로젝트 세팅")))
		{
			if (Editor::ProjectSettings)
			{
				Editor::ProjectSettings->SetVisible(true);
			}
		}
		ImGui::EndMenu();
	}
}

