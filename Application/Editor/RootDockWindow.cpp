#include "pch.h"
#include "RootDockWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"

#include "Editor/Editor.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#include "Editor/Main/MainDockWindow.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/String/StringUtillity.h"

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
		if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, File::Path(absolutePath)))
		{
			Core::SceneManager->PreloadReferencedAssets(*scene);
			if (const EngineCore* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineCore() : nullptr)
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

				CAudioSystem* audioSystem = scene->FindSystem<CAudioSystem>();
				if (nullptr == audioSystem)
				{
					audioSystem = scene->AddSystem<CAudioSystem>(
						context->Audio, context->AssetManager);
				}
				if (nullptr != audioSystem)
				{
					audioSystem->SetDevice(context->Audio);
					audioSystem->SetAssetManager(context->AssetManager);
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
				if (ESceneSerializeResult::Success == serializer.SaveToFile(*scene, Editor::GetActiveScenePath()))
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
				const Vector2 camPos  = Editor::SceneView->GetEditorCameraPos();
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
		//ImText(Loc::Text("root.no_project_loaded"), ImText::Align::Center);
	}

	// 비동기 자산 로드 진행 중이면 프로그레스 팝업을 띄운다 (idempotent).
	EnsureProjectLoadingPopup();

	// 프로젝트 로드 직후(전이 감지) 자산 정합성 요약을 1회 평가.
	MaybeShowReconcileSummary();

	// 비동기 프로젝트 로드가 끝나면, 그때 마지막 씬을 메인 스레드에서 로드한다.
	// (자산 임포트/스크립트 빌드 태스크 완료 후라 참조 에셋이 모두 준비된 상태.)
	if (m_pendingLoadLastScene)
	{
		if (false == projectManager.IsValid() || false == projectManager->IsProjectLoaded())
		{
			m_pendingLoadLastScene = false;   // 프로젝트가 닫혔거나 로드 실패 — 취소.
		}
		else if (false == projectManager->HasLoadingTasks())
		{
			m_pendingLoadLastScene = false;
			TryLoadLastScene(projectManager);
		}
	}
}

// 같은 Id 의 팝업이 이미 열려 있으면 OpenPopup 은 기존 핸들을 그대로 반환하므로
// 매 프레임 호출해도 안전 (중복 생성 X).
void CRootDockWindow::EnsureProjectLoadingPopup()
{
	if (false == Editor::ImEditor.IsValid())
		return;

	SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
	if (false == pm.IsValid() || false == pm->HasLoadingTasks())
	{
		return;
	}

	ImPopupDesc desc;
	desc.Title           = Loc::Text("project.loading.title");
	desc.Id              = "##project_loading_popup";
	desc.Kind            = EImPopupKind::Modal;
	desc.Flags           = ImGuiWindowFlags_NoResize
	                     | ImGuiWindowFlags_NoMove
	                     | ImGuiWindowFlags_NoCollapse
	                     | ImGuiWindowFlags_NoSavedSettings;
	desc.InitSize        = ImVec2(420.0f, 0.0f);   // 폭 고정 + 높이 자동 — Auto resize 미사용.
	desc.ShowCloseButton = false;
	desc.OnRenderStayFunc = [this](IImPopupWindow& popup)
	{
		RenderProjectLoadingPopup(popup);
	};

	m_loadingPopupHandle = Editor::ImEditor->OpenPopup(desc);
}

void CRootDockWindow::MaybeShowReconcileSummary()
{
	SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	const bool loaded = pm.IsValid() && pm->IsProjectLoaded();

	// 프로젝트 닫힘 → 다음 로드를 위해 전이 플래그 리셋.
	if (false == loaded)
	{
		m_wasProjectLoaded = false;
		return;
	}

	// 로드 전이(false→true)를 잡아 1회만 평가.
	if (m_wasProjectLoaded)
	{
		return;
	}
	m_wasProjectLoaded = true;

	const AssetReconcileReport& r = pm->GetLastReconcileReport();
	// 사용자가 알아야 할 "치유"가 있었을 때만 팝업. 깨끗하면 조용히 지나간다.
	const bool notable = (r.MetaGenerated + r.GuidRecovered + r.Relinked + r.DuplicateResolved + r.OrphanQuarantined + r.Failed) > 0;
	if (false == notable)
	{
		return;
	}

	m_reconcileSummary = r;

	ImPopupDesc desc;
	desc.Title           = Loc::Text("reconcile.title");
	desc.Id              = "##asset_reconcile_summary";
	desc.Kind            = EImPopupKind::Modal;
	desc.InitSize        = ImVec2(440.0f, 0.0f);
	desc.ShowCloseButton = true;
	desc.OnRenderStayFunc = [this](IImPopupWindow& popup)
	{
		RenderReconcileSummaryPopup(popup);
	};
	Editor::ImEditor->OpenPopup(desc);
}

void CRootDockWindow::RenderReconcileSummaryPopup(IImPopupWindow& popup)
{
	const AssetReconcileReport& r = m_reconcileSummary;

	ImGui::TextWrapped("%s", Loc::Text("reconcile.summary"));
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	auto row = [](const char* labelKey, int value, const ImVec4& color)
	{
		if (value <= 0) return;
		ImGui::TextColored(color, "%s", Loc::Text(labelKey));
		ImGui::SameLine(220.0f);
		ImGui::Text("%d", value);
	};

	const ImVec4 good(0.40f, 0.85f, 0.40f, 1.0f);
	const ImVec4 warn(0.95f, 0.80f, 0.35f, 1.0f);
	const ImVec4 bad (0.90f, 0.35f, 0.35f, 1.0f);

	row("reconcile.generated",  r.MetaGenerated,     good);
	row("reconcile.recovered",  r.GuidRecovered,     good);
	row("reconcile.relinked",   r.Relinked,          good);
	row("reconcile.dup_resolved", r.DuplicateResolved, warn);
	row("reconcile.orphan",     r.OrphanQuarantined, warn);
	row("reconcile.failed",     r.Failed,            bad);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ImGui::Button(Loc::Text("common.ok"), ImVec2(120.0f, 0.0f)))
	{
		popup.Close();
	}
}

void CRootDockWindow::RenderProjectLoadingPopup(IImPopupWindow& popup)
{
	SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	if (false == pm.IsValid())
	{
		popup.Close();
		return;
	}

	const std::uint32_t done  = pm->GetLoadCompletedCount();
	const std::uint32_t total = pm->GetLoadTotalCount();
	const float         frac  = pm->GetLoadProgress01();

	ImGui::TextUnformatted(Loc::Text("project.loading.body"));
	ImGui::Spacing();

	char overlay[64];
	std::snprintf(overlay, sizeof(overlay), "%u / %u", done, total);
	ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), overlay);

	// ── 작업 목록 — 프로그레스 바 아래에 각 태스크를 나열 ──────────────────
	// 완료 태스크는 초록색, 미완료는 빨간색. 각 항목 앞에 LoadingSpinner 를 그린다.
	const std::vector<TaskProgressInfo> tasks = pm->GetLoadTaskSnapshot();
	if (false == tasks.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const ImVec4 completedColor(0.40f, 0.85f, 0.40f, 1.0f); // 초록
		const ImVec4 pendingColor  (0.90f, 0.35f, 0.35f, 1.0f); // 빨강
		const float  spinnerGap    = ImGui::GetStyle().ItemInnerSpacing.x;

		// 항목이 많아도 팝업이 과도하게 커지지 않도록 스크롤 영역으로 감싼다.
		const float  rowHeight   = ImGui::GetFrameHeightWithSpacing();
		const float  visibleRows = tasks.size() < 8 ? static_cast<float>(tasks.size()) : 8.0f;
		const float  listHeight  = visibleRows * rowHeight;
		if (ImGui::BeginChild("##project_loading_tasks", ImVec2(0.0f, listHeight), false))
		{
			for (std::size_t i = 0; i < tasks.size(); ++i)
			{
				const TaskProgressInfo& info = tasks[i];
				const ImVec4& color = info.Completed ? completedColor : pendingColor;

				ImGui::PushID(static_cast<int>(i));
				// 완료 태스크는 체크(✓), 진행 중 태스크는 스피너.
				if (info.Completed)
				{
					ImGui::Utillity::CheckMark(0.0f, color);
				}
				else
				{
					ImGui::Utillity::LoadingSpinner(0.0f, color);
				}
				ImGui::SameLine(0.0f, spinnerGap);
				ImGui::AlignTextToFramePadding();
				const char* label = false == info.Description.empty() ? info.Description.c_str() : info.Name.c_str();
				ImGui::TextColored(color, "%s", label);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	// 모든 작업이 끝나면 자동 닫기.
	if (false == pm->HasLoadingTasks())
	{
		popup.Close();
		m_loadingPopupHandle = INVALID_POPUP_HANDLE;
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

						// 마지막으로 열었던 씬 자동 로드 — 비동기 자산 로드가 끝난 뒤로 지연.
						// (여기서 동기 호출하면 PreloadReferencedAssets 가 메인 스레드를 막아
						//  비동기 로드가 무의미해지고 에디터가 프리즈된다. OnRenderStay 에서
						//  HasLoadingTasks()==false 가 되면 처리한다.)
						m_pendingLoadLastScene = true;
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
			Editor::ProjectSettings->Focus();
		}
	}
}
