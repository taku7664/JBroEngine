#include "pch.h"
#include "RootDockWindow.h"

#include "Editor/Editor.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#include "Editor/Main/MainDockWindow.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
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

			if (Core::Localization.IsValid())
			{
				pm->SetEditorLocaleCode(Core::Localization->GetCurrentLocale());
			}

			std::size_t imguiIniSize = 0;
			const char* imguiIni = ImGui::SaveIniSettingsToMemory(&imguiIniSize);
			pm->SetImGuiIniSettings((nullptr != imguiIni && imguiIniSize > 0) ? std::string(imguiIni, imguiIniSize) : std::string());

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

    SetLocalizedTitleKey("window.root");

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
		ImText(Loc::Text("root.no_project_loaded"), ImText::Align::Center);
	}
}

void CRootDockWindow::OpenNewProjectPopup(const File::Path& parentFolder)
{
	m_newProjectParentFolder = parentFolder;
	m_newProjectNameBuf.fill(0);
	m_newProjectError.clear();
	ImGui::OpenPopup("##new_project_popup");

	ImPopupDesc desc;
	desc.Title = Loc::Text("menu.file.new_project");
	desc.OnRenderStayFunc = [this](IImPopupWindow& popup)
	{
		RenderNewProjectPopup(popup);
		};
	Editor::ImEditor->OpenPopup(desc);
}

void CRootDockWindow::RenderNewProjectPopup(IImPopupWindow& popup)
{
	//ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Always);
	//ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGui::SeparatorText(Loc::Text("menu.file.new_project"));

	ImGui::Spacing();
	ImGui::Text("%s: %s", Loc::Text("new_project.location"), m_newProjectParentFolder.string().c_str());
	ImGui::Spacing();

	ImGui::SetNextItemWidth(-1.0f);
	const bool enterPressed = ImGui::InputText(
		"##project_name",
		m_newProjectNameBuf.data(),
		m_newProjectNameBuf.size(),
		ImGuiInputTextFlags_EnterReturnsTrue);

	ImGui::TextDisabled("%s", Loc::Text("new_project.name_label"));

	if (false == m_newProjectError.empty())
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_newProjectError.c_str());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	const bool canCreate = m_newProjectNameBuf[0] != '\0';

	if ((enterPressed || ImGui::Button(Loc::Text("common.create"), ImVec2(120.0f, 0.0f))) && canCreate)
	{
		SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
		if (pm.IsValid())
		{
			const std::string projectName(m_newProjectNameBuf.data());
			if (pm->CreateProject(m_newProjectParentFolder, projectName))
			{
				m_customDockFlags -= IMDOCKWINDOW_FLAG_NO_DOCKING;
				Editor::MainDockWindow = Editor::ImEditor->CreateImWindow<CMainDockWindow>("MainDockWindow");
				if (Editor::RootDockWindow && Editor::MainDockWindow)
				{
					Editor::RootDockWindow->AddChildImWindow(DynamicSafePtrCast<IImWindow>(Editor::MainDockWindow));
				}
				popup.Close();
			}
			else
			{
				m_newProjectError = Loc::Text("new_project.create_failed");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button(Loc::Text("common.cancel"), ImVec2(120.0f, 0.0f)))
	{
		popup.Close();
	}
}

void CRootDockWindow::OnMenuBar()
{
	if (ImGui::BeginMenu(Loc::Text("menu.file")))
	{
		if (ImGui::MenuItem(Loc::Text("menu.file.new_project")))
		{
			File::Path parentFolder;
			if (File::ShowOpenFolderDialog(nullptr, L"프로젝트를 만들 폴더 선택", L"", parentFolder))
			{
				OpenNewProjectPopup(parentFolder);
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text("menu.file.open_project")))
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
							Editor::RootDockWindow->AddChildImWindow(DynamicSafePtrCast<IImWindow>(Editor::MainDockWindow));
						}
						if (Editor::MainDockWindow && false == projectManager->GetImGuiIniSettings().empty())
						{
							Editor::MainDockWindow->UseStoredDockLayout();
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
		if (ImGui::MenuItem(Loc::Text("menu.file.save_project")))
		{
			SaveCurrentEditorState();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Loc::Text("menu.settings")))
	{
		if (ImGui::MenuItem(Loc::Text("menu.settings.project_settings")))
		{
			if (Editor::ProjectSettings)
			{
				Editor::ProjectSettings->SetVisible(true);
			}
		}
		ImGui::EndMenu();
	}

	//ImGui::Begin("StyleEditor", nullptr, ImGuiWindowFlags_NoDocking);
	//ImGui::ShowStyleEditor();
	//ImGui::End();

	// 메뉴바 우측에 스크립트 빌드 상태 (스피너 + 텍스트). 다른 메뉴는 전부 좌측에 채우고,
	// 이 호출이 마지막이라 남은 공간 끝에 우측 정렬로 그려진다.
	DrawLiveCompileMenuBarStatus();
}

void CRootDockWindow::DrawLiveCompileMenuBarStatus()
{
	SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	if (false == pm.IsValid())
	{
		return;
	}

	const ELiveCompileState state = pm->GetLiveCompileState();

	// ── 상태 전이 감지 ────────────────────────────────────────────────────────
	if (state != m_lastCompileState)
	{
		if (state == ELiveCompileState::Compiling)
		{
			m_compileElapsedSeconds = 0.0f;
		}
		else if (m_lastCompileState == ELiveCompileState::Compiling &&
		         (state == ELiveCompileState::Loaded || state == ELiveCompileState::Failed))
		{
			// 컴파일 → 완료/실패 전이 시점에 잔존 타이머 설정
			m_resultLingerSuccess   = (state == ELiveCompileState::Loaded);
			m_resultLingerRemaining = m_resultLingerSuccess ? 1.5f : 6.0f;
		}
		m_lastCompileState = state;
	}

	// ── 시간 누적 ─────────────────────────────────────────────────────────────
	const float dt = ImGui::GetIO().DeltaTime;
	if (state == ELiveCompileState::Compiling)
	{
		m_compileElapsedSeconds += dt;
	}
	if (m_resultLingerRemaining > 0.0f)
	{
		m_resultLingerRemaining -= dt;
		if (m_resultLingerRemaining < 0.0f)
		{
			m_resultLingerRemaining = 0.0f;
		}
	}

	const bool showSpinner = (state == ELiveCompileState::Compiling);
	const bool showResult  = (m_resultLingerRemaining > 0.0f);
	if (false == showSpinner && false == showResult)
	{
		return;
	}

	// ── 라벨/색상 결정 ────────────────────────────────────────────────────────
	char  label[96];
	ImVec4 textColor;
	if (showSpinner)
	{
		std::snprintf(label, sizeof(label), "%s  %.1fs",
		              Loc::Text("script.status.building"),
		              m_compileElapsedSeconds);
		textColor = ImVec4(0.95f, 0.85f, 0.4f, 1.0f);
	}
	else if (m_resultLingerSuccess)
	{
		std::snprintf(label, sizeof(label), "%s", Loc::Text("script.status.loaded"));
		textColor = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
	}
	else
	{
		std::snprintf(label, sizeof(label), "%s", Loc::Text("script.status.failed"));
		textColor = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
	}

	// ── 우측 정렬: 그릴 영역 너비를 계산해서 cursor 를 끝에서 빼서 배치 ───────
	const float spinnerW   = showSpinner ? ImGui::GetFrameHeight() : 0.0f;
	const float gap        = showSpinner ? 8.0f : 0.0f;
	const float textW      = ImGui::CalcTextSize(label).x;
	const float marginR    = 12.0f;
	const float totalW     = spinnerW + gap + textW + marginR;

	// 메뉴바는 horizontal layout — 현재 cursor 이후의 남은 너비에서 totalW 만큼 빼고 우측 정렬
	const float avail = ImGui::GetContentRegionAvail().x;
	if (avail > totalW)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - totalW));
	}

	if (showSpinner)
	{
		ImGui::Utillity::LoadingSpinner(0.0f, textColor);
		ImGui::SameLine(0.0f, gap);
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored(textColor, "%s", label);

	// 실패 시 클릭하면 ProjectSettings 창 열기 (자세한 메시지 확인 유도)
	if (showResult && false == m_resultLingerSuccess && ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		if (Editor::ProjectSettings)
		{
			Editor::ProjectSettings->SetVisible(true);
		}
	}
}

