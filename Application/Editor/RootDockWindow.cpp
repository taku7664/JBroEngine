#include "pch.h"
#include "RootDockWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"

#include "Editor/Editor.h"
#include "Editor/Build/BuildSettingsUtils.h"
#include "Editor/EditorContext.h"
#include "Editor/Main/BuildSettingsWindow.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#include "Editor/Main/MainDockWindow.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/RuntimeConfig.h"
#include "Engine/Core/ScriptCore.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Rendering/TextRenderSystem.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/String/StringUtillity.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

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

		if (false == Engine.SceneManager.IsValid())
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

		CGameScene* scene = Engine.SceneManager->CreateScene(lastScenePath.c_str());
		if (nullptr == scene)
		{
			CSystemLog::Error("Failed to create scene for last opened scene.");
			return;
		}

		CSceneSerializer serializer;
		if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, File::Path(absolutePath)))
		{
			Engine.SceneManager->AcquireReferencedAssets(*scene);
			if (const EngineCore* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineCore() : nullptr)
			{
				CSpriteAnimationSystem* animationSystem = scene->FindSystem<CSpriteAnimationSystem>();
				if (nullptr == animationSystem)
				{
					animationSystem = scene->AddSystem<CSpriteAnimationSystem>(context->AssetManager);
				}
				if (nullptr != animationSystem)
				{
					animationSystem->SetAssetManager(context->AssetManager);
				}

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
						context->Renderer.TryGet(),
						context->RenderResourceCache.TryGet(),
						Runtime.PixelsPerUnit);
				}

				CShapeRenderSystem* shapeSystem = scene->FindSystem<CShapeRenderSystem>();
				if (nullptr == shapeSystem)
				{
					shapeSystem = scene->AddSystem<CShapeRenderSystem>(context->RenderScene.TryGet());
				}
				if (nullptr != shapeSystem)
				{
					shapeSystem->SetRenderScene(context->RenderScene.TryGet());
					shapeSystem->SetDependencies(context->RHIDevice.TryGet(), context->Renderer.TryGet());
				}

				CTextRenderSystem* textSystem = scene->FindSystem<CTextRenderSystem>();
				if (nullptr == textSystem)
				{
					textSystem = scene->AddSystem<CTextRenderSystem>(context->RenderScene.TryGet());
				}
				if (nullptr != textSystem)
				{
					textSystem->SetRenderScene(context->RenderScene.TryGet());
					textSystem->SetDependencies(context->AssetManager.TryGet(), context->RHIDevice.TryGet(), context->Renderer.TryGet(),
						Runtime.PixelsPerUnit, Runtime.DefaultFontFamilyGuid, Runtime.FallbackFontFamilies);
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
			Engine.SceneManager->SetActiveScene(lastScenePath.c_str());
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

    SetLocalizedTitleKey(EditorLocKeys::WindowRoot);

    // 프로젝트 세팅 창 (플로팅, 처음에는 숨김)
    if (Editor::ImEditor)
    {
        Editor::ProjectSettings =
            Editor::ImEditor->CreateImWindow<CProjectSettingsWindow>("ProjectSettings", 0);
        Editor::BuildSettings =
            Editor::ImEditor->CreateImWindow<CBuildSettingsWindow>("BuildSettings", 0);
    }
}

void CRootDockWindow::OnUpdate()
{
	Editor::ShortcutManager.ProcessInput();
}

void CRootDockWindow::OnRenderStay()
{
	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();

	if (projectManager && false == projectManager->IsProjectLoaded())
	{
		//ImText(Loc::Text(EditorLocKeys::RootNoProjectLoaded), ImText::Align::Center);
	}

	// 비동기 자산 로드 진행 중이면 프로그레스 팝업을 띄운다 (idempotent).
	EnsureProjectLoadingPopup();

	// 일괄 빌드 큐: 직전 플랫폼 빌드가 끝났으면 다음 플랫폼을 이어서 빌드한다.
	AdvanceBuildQueueIfReady();

	// 게임 빌드(패키징) 진행 중이면 진행 팝업을 띄운다 (idempotent — 빌드 아니면 즉시 반환).
	EnsureBuildProgressPopup();

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

	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid() || false == pm->HasLoadingTasks())
	{
		return;
	}

	ImPopupDesc desc;
	desc.Title           = Loc::Text(EditorLocKeys::ProjectLoadingTitle);
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
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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
	desc.Title           = Loc::Text(EditorLocKeys::ReconcileTitle);
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

	ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::ReconcileSummary));
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

	row(EditorLocKeys::ReconcileGenerated,  r.MetaGenerated,     good);
	row(EditorLocKeys::ReconcileRecovered,  r.GuidRecovered,     good);
	row(EditorLocKeys::ReconcileRelinked,   r.Relinked,          good);
	row(EditorLocKeys::ReconcileDupResolved, r.DuplicateResolved, warn);
	row(EditorLocKeys::ReconcileOrphan,     r.OrphanQuarantined, warn);
	row(EditorLocKeys::ReconcileFailed,     r.Failed,            bad);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(120.0f, 0.0f)))
	{
		popup.Close();
	}
}

void CRootDockWindow::RenderProjectLoadingPopup(IImPopupWindow& popup)
{
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid())
	{
		popup.Close();
		return;
	}

	const std::uint32_t done  = pm->GetLoadCompletedCount();
	const std::uint32_t total = pm->GetLoadTotalCount();
	const float         frac  = pm->GetLoadProgress01();

	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ProjectLoadingBody));
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
	if (false == Editor::ImEditor.IsValid())
	{
		return;
	}

	m_newProjectParentFolder = parentFolder;
	m_newProjectNameBuf.fill(0);
	m_newProjectError.clear();

	// 프로젝트 표준 팝업 시스템(IImPopupWindow)으로 제어한다. 원시 ImGui::OpenPopup 은
	// 이 시스템과 맞지 않아 사용하지 않는다. 동일 Id 면 OpenPopup 이 중복 생성하지 않는다.
	ImPopupDesc desc;
	desc.Title            = Loc::Text(EditorLocKeys::MenuFileNewProject);
	desc.Id               = "##new_project_popup";
	desc.Kind             = EImPopupKind::Modal;
	desc.InitSize         = ImVec2(400.0f, 0.0f);
	desc.ShowCloseButton  = true;
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

	ImGui::SeparatorText(Loc::Text(EditorLocKeys::MenuFileNewProject));

	ImGui::Spacing();
	ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::NewProjectLocation), m_newProjectParentFolder.string().c_str());
	ImGui::Spacing();

	ImGui::SetNextItemWidth(-1.0f);
	const bool enterPressed = ImGui::InputText(
		"##project_name",
		m_newProjectNameBuf.data(),
		m_newProjectNameBuf.size(),
		ImGuiInputTextFlags_EnterReturnsTrue);

	ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::NewProjectNameLabel));

	if (false == m_newProjectError.empty())
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_newProjectError.c_str());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	const bool canCreate = m_newProjectNameBuf[0] != '\0';

	if ((enterPressed || ImGui::Button(Loc::Text(EditorLocKeys::CommonCreate), ImVec2(120.0f, 0.0f))) && canCreate)
	{
		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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
				m_newProjectError = Loc::Text(EditorLocKeys::NewProjectCreateFailed);
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button(Loc::Text(EditorLocKeys::CommonCancel), ImVec2(120.0f, 0.0f)))
	{
		popup.Close();
	}
}

void CRootDockWindow::OnMenuBar()
{
	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuFile)))
	{
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuFileNewProject)))
		{
			File::Path parentFolder;
			const std::wstring title = Utillity::U8ToWString(Loc::Text(EditorLocKeys::FileDialogNewProjectFolderTitle));
			if (File::ShowOpenFolderDialog(ImGui::Utillity::GetDialogOwnerHandle(), title.c_str(), L"", parentFolder))
			{
				OpenNewProjectPopup(parentFolder);
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuFileOpenProject)))
		{
			File::Path projectPath;
			const std::wstring title = Utillity::U8ToWString(Loc::Text(EditorLocKeys::FileDialogOpenProjectTitle));
			const std::wstring filterName = Utillity::U8ToWString(Loc::Text(EditorLocKeys::FileDialogFilterJbroProject));
			if (File::ShowOpenFileDialog(
				ImGui::Utillity::GetDialogOwnerHandle(),
				title.c_str(),
				L"",
				{ { filterName.c_str(), L"*.Jproject" }, { L"All Files", L"*.*" } },
				projectPath))
			{
				SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
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
		const bool canSave = Editor::ShortcutManager.CanExecute(EEditorShortcut::SaveProject);
		if (false == canSave)
		{
			ImGui::BeginDisabled();
		}
		const std::string saveShortcut = Editor::ShortcutManager.GetShortcutText(EEditorShortcut::SaveProject);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuFileSaveProject), saveShortcut.c_str()))
		{
			Editor::ShortcutManager.Execute(EEditorShortcut::SaveProject);
		}
		if (false == canSave
			&& EditorSimulationGuard::IsSimulationActive()
			&& ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", EditorSimulationGuard::GetSaveBlockedMessage());
		}
		if (false == canSave)
		{
			ImGui::EndDisabled();
		}

		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
		const bool canBuild = pm.IsValid()
			&& pm->IsProjectLoaded()
			&& false == m_buildManager.IsRunning()
			&& EditorSimulationGuard::CanBuildProject();
		if (false == canBuild)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuFileBuild)))
		{
			const ProjectBuildSettings& buildSettings = pm->GetBuildSettings();
			const std::vector<EBuildTargetPlatform> enabled = BuildSettingsUtils::GetEnabledPlatforms(buildSettings);

			if (enabled.empty())
			{
				ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsNoPlatformEnabled));
			}

			// 활성 플랫폼마다 메뉴아이템. 필수설정 누락이면 빨강 + 클릭 시 빌드세팅의 해당 카테고리로 유도.
			std::size_t blockedCount = 0;
			for (EBuildTargetPlatform platform : enabled)
			{
				const bool requiredIssue = BuildSettingsUtils::HasRequiredIssueForPlatform(buildSettings, platform);
				if (requiredIssue)
				{
					++blockedCount;
				}

				ImGui::Utillity::IDGroup idGroup(BuildSettingsUtils::GetPlatformLabelKey(platform));
				ImGui::Utillity::StyleBuilder style;
				if (requiredIssue)
				{
					style.PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
				}
				if (ImGui::MenuItem(Loc::Text(BuildSettingsUtils::GetPlatformLabelKey(platform))))
				{
					if (requiredIssue)
					{
						if (Editor::BuildSettings)
						{
							Editor::BuildSettings->SetVisible(true);
							Editor::BuildSettings->FocusPlatformCategory(platform);
							Editor::BuildSettings->Focus();
						}
						OpenBuildBlockedPopup(Loc::Text(EditorLocKeys::BuildSettingsRequiredMissingBody));
					}
					else
					{
						StartGameBuild(platform);
					}
				}
			}

			// 활성 플랫폼이 2개 이상이면 일괄 빌드(순차) 메뉴아이템을 추가로 제공한다.
			if (enabled.size() >= 2)
			{
				ImGui::Separator();
				ImGui::Utillity::StyleBuilder style;
				if (blockedCount > 0)
				{
					style.PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
				}
				if (ImGui::MenuItem(Loc::Text(EditorLocKeys::BuildSettingsBuildAll)))
				{
					if (blockedCount > 0)
					{
						if (Editor::BuildSettings)
						{
							Editor::BuildSettings->SetVisible(true);
							Editor::BuildSettings->FocusFirstInvalidCategory();
							Editor::BuildSettings->Focus();
						}
						OpenBuildBlockedPopup(Loc::Text(EditorLocKeys::BuildSettingsRequiredMissingBody));
					}
					else
					{
						StartBatchGameBuild(enabled);
					}
				}
			}
			ImGui::EndMenu();
		}
		if (false == canBuild
			&& EditorSimulationGuard::IsSimulationActive()
			&& ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", EditorSimulationGuard::GetBuildBlockedMessage());
		}
		if (false == canBuild)
		{
			ImGui::EndDisabled();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuSettings)))
	{
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuSettingsProjectSettings)))
		{
			if (Editor::ProjectSettings)
			{
				Editor::ProjectSettings->SetVisible(true);
			}
		}
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::MenuSettingsBuildSettings)))
		{
			if (Editor::BuildSettings)
			{
				Editor::BuildSettings->SetVisible(true);
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
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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
		              Loc::Text(EditorLocKeys::ScriptStatusBuilding),
		              m_compileElapsedSeconds);
		textColor = ImVec4(0.95f, 0.85f, 0.4f, 1.0f);
	}
	else if (m_resultLingerSuccess)
	{
		std::snprintf(label, sizeof(label), "%s", Loc::Text(EditorLocKeys::ScriptStatusLoaded));
		textColor = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
	}
	else
	{
		std::snprintf(label, sizeof(label), "%s", Loc::Text(EditorLocKeys::ScriptStatusFailed));
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

void CRootDockWindow::StartGameBuild(EBuildTargetPlatform platform)
{
	// 단일 빌드도 큐에 1개만 넣어 일괄빌드와 진행 흐름을 통일한다.
	StartBatchGameBuild({ platform });
}

void CRootDockWindow::StartBatchGameBuild(const std::vector<EBuildTargetPlatform>& platforms)
{
	if (platforms.empty() || m_buildManager.IsRunning())
	{
		return;
	}

	if (false == EditorSimulationGuard::CanBuildProject())
	{
		OpenBuildBlockedPopup(EditorSimulationGuard::GetBuildBlockedMessage());
		return;
	}

	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (Editor::BuildSettings && Editor::BuildSettings->HasUnsavedChanges())
	{
		Editor::BuildSettings->SetVisible(true);
		Editor::BuildSettings->Focus();
		OpenBuildBlockedPopup(Loc::Text(EditorLocKeys::BuildSettingsApplyRequiredBody));
		return;
	}

	m_buildQueue = platforms;
	m_buildQueueIndex = 0;
	m_buildManager.StartBuild(pm, m_buildQueue[m_buildQueueIndex]);
	EnsureBuildProgressPopup();
}

void CRootDockWindow::AdvanceBuildQueueIfReady()
{
	// 큐에 다음 플랫폼이 남아있고, 직전 빌드가 성공으로 끝났으면 다음 플랫폼을 빌드한다.
	// 실패하면 큐를 중단한다(진행 팝업이 실패 상태를 그대로 보여준다).
	if (m_buildQueueIndex + 1 >= m_buildQueue.size() || m_buildManager.IsRunning())
	{
		return;
	}

	const GameBuildSnapshot snapshot = m_buildManager.GetSnapshot();
	if (snapshot.State == EGameBuildState::Succeeded)
	{
		if (false == EditorSimulationGuard::CanBuildProject())
		{
			m_buildQueue.clear();
			m_buildQueueIndex = 0;
			OpenBuildBlockedPopup(EditorSimulationGuard::GetBuildBlockedMessage());
			return;
		}

		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
		++m_buildQueueIndex;
		m_buildManager.StartBuild(pm, m_buildQueue[m_buildQueueIndex]);
		EnsureBuildProgressPopup();
	}
	else if (snapshot.State == EGameBuildState::Failed)
	{
		m_buildQueue.clear();
		m_buildQueueIndex = 0;
	}
}

void CRootDockWindow::OpenBuildBlockedPopup(std::string message)
{
	if (false == Editor::ImEditor.IsValid())
	{
		return;
	}

	m_buildBlockedMessage = std::move(message);
	ImPopupDesc desc;
	desc.Title = Loc::Text(EditorLocKeys::BuildSettingsApplyRequired);
	desc.Id = "##build_settings_apply_required_popup";
	desc.Kind = EImPopupKind::Modal;
	desc.InitSize = ImVec2(420.0f, 0.0f);
	desc.ShowCloseButton = true;
	desc.OnRenderStayFunc = [this](IImPopupWindow& popup)
		{
			RenderBuildBlockedPopup(popup);
		};
	m_buildBlockedPopupHandle = Editor::ImEditor->OpenPopup(desc);
}

void CRootDockWindow::RenderBuildBlockedPopup(IImPopupWindow& popup)
{
	ImGui::TextWrapped("%s", m_buildBlockedMessage.c_str());
	ImGui::Spacing();
	if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(120.0f, 0.0f)))
	{
		popup.Close();
		m_buildBlockedPopupHandle = INVALID_POPUP_HANDLE;
	}
}

void CRootDockWindow::EnsureBuildProgressPopup()
{
	if (false == Editor::ImEditor.IsValid())
	{
		return;
	}

	const GameBuildSnapshot snapshot = m_buildManager.GetSnapshot();
	if (snapshot.State == EGameBuildState::Idle)
	{
		return;
	}

	if (Editor::ImEditor->IsPopupOpen(m_buildPopupHandle))
	{
		return;
	}

	ImPopupDesc desc;
	desc.Title = Loc::Text(EditorLocKeys::BuildProgressTitle);
	desc.Id = "##game_build_progress_popup";
	desc.Kind = EImPopupKind::Modal;
	desc.Flags = ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings;
	desc.InitSize = ImVec2(460.0f, 0.0f);
	desc.ShowCloseButton = false;
	desc.OnRenderStayFunc = [this](IImPopupWindow& popup)
		{
			RenderBuildProgressPopup(popup);
		};
	m_buildPopupHandle = Editor::ImEditor->OpenPopup(desc);
}

void CRootDockWindow::RenderBuildProgressPopup(IImPopupWindow& popup)
{
	GameBuildSnapshot snapshot = m_buildManager.GetSnapshot();

	const float progress = snapshot.TotalCount > 0 ? snapshot.Progress01 : 0.0f;
	char overlay[64];
	std::snprintf(overlay, sizeof(overlay), "%u / %u", snapshot.CompletedCount, snapshot.TotalCount);

	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::BuildProgressBody));
	ImGui::Spacing();
	ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), overlay);

	if (false == snapshot.Tasks.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const ImVec4 completedColor(0.40f, 0.85f, 0.40f, 1.0f);
		const ImVec4 runningColor(0.95f, 0.85f, 0.4f, 1.0f);
		const ImVec4 failedColor(0.90f, 0.35f, 0.35f, 1.0f);
		const ImVec4 pendingColor(0.65f, 0.65f, 0.65f, 1.0f);
		const float spinnerGap = ImGui::GetStyle().ItemInnerSpacing.x;
		const float rowHeight = ImGui::GetFrameHeightWithSpacing();
		const float visibleRows = snapshot.Tasks.size() < 8 ? static_cast<float>(snapshot.Tasks.size()) : 8.0f;
		const float listHeight = visibleRows * rowHeight;

		if (ImGui::BeginChild("##game_build_tasks", ImVec2(0.0f, listHeight), false))
		{
			for (std::size_t i = 0; i < snapshot.Tasks.size(); ++i)
			{
				const GameBuildTaskProgress& task = snapshot.Tasks[i];
				ImGui::PushID(static_cast<int>(i));

				ImVec4 color = pendingColor;
				if (task.State == EGameBuildTaskState::Completed) color = completedColor;
				else if (task.State == EGameBuildTaskState::Running) color = runningColor;
				else if (task.State == EGameBuildTaskState::Failed) color = failedColor;

				if (task.State == EGameBuildTaskState::Completed)
				{
					ImGui::Utillity::CheckMark(0.0f, color);
				}
				else if (task.State == EGameBuildTaskState::Running)
				{
					ImGui::Utillity::LoadingSpinner(0.0f, color);
				}
				else
				{
					ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
				}
				ImGui::SameLine(0.0f, spinnerGap);
				ImGui::AlignTextToFramePadding();
				const char* label = false == task.Description.empty() ? task.Description.c_str() : task.Name.c_str();
				ImGui::TextColored(color, "%s", label);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	if (snapshot.State == EGameBuildState::Succeeded)
	{
		if (false == snapshot.OutputOpened && false == snapshot.PackageDirectory.empty())
		{
			File::OpenFile(snapshot.PackageDirectory);
			m_buildManager.MarkOutputOpened();
			snapshot.OutputOpened = true;
		}
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "%s", Loc::Text(EditorLocKeys::BuildProgressSucceeded));
		ImGui::TextWrapped("%s", snapshot.PackageDirectory.generic_string().c_str());
		ImGui::Spacing();
		if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(120.0f, 0.0f)))
		{
			popup.Close();
			m_buildPopupHandle = INVALID_POPUP_HANDLE;
			m_buildManager.ClearResult();
		}
	}
	else if (snapshot.State == EGameBuildState::Failed)
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "%s", Loc::Text(EditorLocKeys::BuildProgressFailed));
		if (false == snapshot.Message.empty())
		{
			ImGui::TextWrapped("%s", snapshot.Message.c_str());
		}
		if (false == snapshot.LogPath.empty())
		{
			if (ImGui::Button(Loc::Text(EditorLocKeys::BuildProgressOpenLog), ImVec2(120.0f, 0.0f)))
			{
				File::OpenFile(snapshot.LogPath);
			}
			ImGui::SameLine();
		}
		if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(120.0f, 0.0f)))
		{
			popup.Close();
			m_buildPopupHandle = INVALID_POPUP_HANDLE;
			m_buildManager.ClearResult();
		}
	}
}
