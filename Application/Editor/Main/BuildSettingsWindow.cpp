#include "pch.h"
#include "BuildSettingsWindow.h"

#include "Editor/Editor.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImItem/ImSplitter.h"
#include "Engine/Editor/ImItem/ImText.h"
#include "Engine/Editor/ImWindow/ImWindowFlag.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Utillity/String/StringUtillity.h"

#include <algorithm>
#include <cwctype>

namespace
{
	const File::Path WINDOWS_ICON_ASSET_PATH = "Package/Windows/AppIcon.ico";

	int ToIndex(EBuildTargetPlatform platform)
	{
		switch (platform)
		{
		case EBuildTargetPlatform::Web: return 1;
		case EBuildTargetPlatform::Android: return 2;
		case EBuildTargetPlatform::IOS: return 3;
		case EBuildTargetPlatform::Windows:
		default: return 0;
		}
	}

	EBuildTargetPlatform ToBuildTargetPlatform(int index)
	{
		switch (index)
		{
		case 1: return EBuildTargetPlatform::Web;
		case 2: return EBuildTargetPlatform::Android;
		case 3: return EBuildTargetPlatform::IOS;
		case 0:
		default: return EBuildTargetPlatform::Windows;
		}
	}

	int ToIndex(EBuildConfiguration configuration)
	{
		return EBuildConfiguration::Debug == configuration ? 0 : 1;
	}

	EBuildConfiguration ToBuildConfiguration(int index)
	{
		return 0 == index ? EBuildConfiguration::Debug : EBuildConfiguration::Release;
	}

	const char* ToString(EBuildTargetPlatform platform)
	{
		switch (platform)
		{
		case EBuildTargetPlatform::Web: return "Web";
		case EBuildTargetPlatform::Android: return "Android";
		case EBuildTargetPlatform::IOS: return "IOS";
		case EBuildTargetPlatform::Windows:
		default: return "Windows";
		}
	}

	const char* ToString(EBuildConfiguration configuration)
	{
		return EBuildConfiguration::Debug == configuration ? "Debug" : "Release";
	}

	SafePtr<CProjectManager> GetProjectManagerForBuildSettings()
	{
		return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	}

	std::string ToUtf8PathString(const File::Path& path)
	{
		const auto text = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
	}

	bool IsSubPath(const File::Path& relativePath)
	{
		if (relativePath.empty())
		{
			return false;
		}

		for (const File::Path& part : relativePath)
		{
			if (part == L"..")
			{
				return false;
			}
		}
		return true;
	}

	bool TryMakeRelativeSubPath(const File::Path& path, const File::Path& root, File::Path& outRelative)
	{
		if (path.empty() || root.empty())
		{
			return false;
		}

		std::error_code ec;
		const File::Path absolutePath = std::filesystem::weakly_canonical(path, ec);
		if (ec)
		{
			return false;
		}
		ec.clear();
		const File::Path absoluteRoot = std::filesystem::weakly_canonical(root, ec);
		if (ec)
		{
			return false;
		}

		const File::Path relativePath = std::filesystem::relative(absolutePath, absoluteRoot, ec);
		if (ec || false == IsSubPath(relativePath))
		{
			return false;
		}

		outRelative = relativePath;
		return true;
	}

	bool HasIcoExtension(const File::Path& path)
	{
		std::wstring extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
			return static_cast<wchar_t>(std::towlower(ch));
		});
		return extension == L".ico";
	}

	std::string MakeBrowseId(const char* suffix)
	{
		std::string id = Loc::Text("common.browse");
		id += suffix;
		return id;
	}

	bool DrawReadOnlyPathWithFolderBrowse(
		const char* inputId,
		std::string& value,
		const char* buttonSuffix,
		const wchar_t* title,
		const std::wstring& initialDirectory)
	{
		const std::string browseId = MakeBrowseId(buttonSuffix);
		const float buttonWidth = ImGui::CalcTextSize(Loc::Text("common.browse")).x
			+ ImGui::GetStyle().FramePadding.x * 2.0f
			+ 8.0f;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::InputText(inputId, &value, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		return ImGui::Utillity::BrowseFolderButton(browseId.c_str(), value, title, initialDirectory.c_str());
	}

	bool DrawReadOnlyPathWithFileBrowse(
		const char* inputId,
		std::string& value,
		const char* buttonSuffix,
		const wchar_t* title,
		const std::wstring& initialDirectory,
		std::vector<File::FileDialogFilter> filters)
	{
		const std::string browseId = MakeBrowseId(buttonSuffix);
		const float buttonWidth = ImGui::CalcTextSize(Loc::Text("common.browse")).x
			+ ImGui::GetStyle().FramePadding.x * 2.0f
			+ 8.0f;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::InputText(inputId, &value, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		return ImGui::Utillity::BrowseFileButton(
			browseId.c_str(), value, title, initialDirectory.c_str(), std::move(filters));
	}
}

void CBuildSettingsWindow::OnCreate()
{
	SetLocalizedTitleKey("window.build_settings");

	m_imguiFlags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoCollapse;

	m_windowFlags = IMWINDOW_FLAG_NONE;

	SetSize({ 760.0f, 560.0f });
	SetVisible(false);
}

void CBuildSettingsWindow::OnShow()
{
	LoadFromProject();
}

void CBuildSettingsWindow::OnRenderStay()
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	if (false == pm.IsValid() || false == pm->IsProjectLoaded())
	{
		ImGui::TextDisabled("%s", Loc::Text("build_settings.no_project"));
		return;
	}
	if (false == m_loadedFromProject && false == m_dirty)
	{
		LoadFromProject();
	}

	constexpr float SPLITTER_W = 3.0f;
	constexpr float MIN_RATIO = 0.18f;
	constexpr float MAX_RATIO = 0.55f;
	constexpr float FOOTER_H = 44.0f;

	const ImVec2 totalAvail = ImGui::GetContentRegionAvail();
	const ImVec2 bodyAvail(totalAvail.x, totalAvail.y - FOOTER_H);
	const float leftW = bodyAvail.x * m_splitRatio - SPLITTER_W * 0.5f;
	const float rightW = bodyAvail.x - leftW - SPLITTER_W;

	ImGui::BeginChild("##build_settings_categories", ImVec2(leftW, bodyAvail.y), true, ImGuiWindowFlags_NoScrollbar);
	DrawCategoryList(leftW);
	ImGui::EndChild();

	ImGui::Utillity::VerticalSplitter("##BuildSettingsSplitter", m_splitRatio, bodyAvail, MIN_RATIO, MAX_RATIO, SPLITTER_W);

	ImGui::BeginChild("##build_settings_content", ImVec2(rightW, bodyAvail.y), true);
	DrawCategoryContent(rightW);
	ImGui::EndChild();

	DrawFooterButtons();
}

void CBuildSettingsWindow::DrawCategoryList(float)
{
	struct CategoryEntry { ECategory Kind; const char* LocKey; };
	static const CategoryEntry categories[] = {
		{ ECategory::General, "build_settings.category.general" },
		{ ECategory::Scenes,  "build_settings.category.scenes"  },
		{ ECategory::Output,  "build_settings.category.output"  },
		{ ECategory::Windows, "build_settings.category.windows" },
		{ ECategory::Android, "build_settings.category.android" },
		{ ECategory::IOS,     "build_settings.category.ios"     },
	};

	for (const CategoryEntry& entry : categories)
	{
		if (ImGui::Selectable(Loc::Text(entry.LocKey), entry.Kind == m_selectedCategory))
		{
			m_selectedCategory = entry.Kind;
		}
	}
}

void CBuildSettingsWindow::DrawCategoryContent(float)
{
	switch (m_selectedCategory)
	{
	case ECategory::General: DrawGeneralCategory(); break;
	case ECategory::Scenes:  DrawScenesCategory();  break;
	case ECategory::Output:  DrawOutputCategory();  break;
	case ECategory::Windows: DrawWindowsCategory(); break;
	case ECategory::Android: DrawAndroidCategory(); break;
	case ECategory::IOS:     DrawIOSCategory();     break;
	default: break;
	}
}

void CBuildSettingsWindow::DrawGeneralCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.general"));

	ImGui::Utillity::FormLayout layout("##build_settings_general", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.product_name.desc"));
			label(Loc::Text("build_settings.product_name"));
		},
		[&]() {
			if (ImGui::InputText("##build.product_name", &m_productName))
			{
				MarkDirty();
			}
		});

	const char* platforms[] = { "Windows", "Web", "Android", "IOS" };
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.target_platform.desc"));
			label(Loc::Text("build_settings.target_platform"));
		},
		[&]() {
			if (ImGui::Combo("##build.target_platform", &m_targetPlatform, platforms, IM_ARRAYSIZE(platforms)))
			{
				MarkDirty();
			}
		});

	const char* configurations[] = { "Debug", "Release" };
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.configuration.desc"));
			label(Loc::Text("build_settings.configuration"));
		},
		[&]() {
			if (ImGui::Combo("##build.configuration", &m_buildConfiguration, configurations, IM_ARRAYSIZE(configurations)))
			{
				MarkDirty();
			}
		});

}

void CBuildSettingsWindow::DrawWindowsCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.windows"));
	if (ToBuildTargetPlatform(m_targetPlatform) != EBuildTargetPlatform::Windows)
	{
		ImGui::TextDisabled("%s", Loc::Text("build_settings.platform_inactive"));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_windows", 4.0f, { 2.0f, 1.0f }, 170.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.windows_icon.desc"));
			label(Loc::Text("build_settings.windows_icon"));
		},
		[&]() {
			DrawWindowsIconSelector();
		});
}

void CBuildSettingsWindow::DrawAndroidCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.android"));
	if (ToBuildTargetPlatform(m_targetPlatform) != EBuildTargetPlatform::Android)
	{
		ImGui::TextDisabled("%s", Loc::Text("build_settings.platform_inactive"));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_android", 4.0f, { 2.0f, 1.0f }, 170.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.android_application_id.desc"));
			label(Loc::Text("build_settings.android_application_id"));
		},
		[&]() {
			if (ImGui::InputText("##build.android_application_id", &m_androidApplicationId))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.android_min_sdk.desc"));
			label(Loc::Text("build_settings.android_min_sdk"));
		},
		[&]() {
			if (ImGui::InputInt("##build.android_min_sdk", &m_androidMinSdkVersion))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.android_target_sdk.desc"));
			label(Loc::Text("build_settings.android_target_sdk"));
		},
		[&]() {
			if (ImGui::InputInt("##build.android_target_sdk", &m_androidTargetSdkVersion))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.android_abi.desc"));
			label(Loc::Text("build_settings.android_abi"));
		},
		[&]() {
			const char* abis[] = { "arm64-v8a", "x86_64" };
			int current = m_androidAbi == "x86_64" ? 1 : 0;
			if (ImGui::Combo("##build.android_abi", &current, abis, IM_ARRAYSIZE(abis)))
			{
				m_androidAbi = abis[current];
				MarkDirty();
			}
		});
}

void CBuildSettingsWindow::DrawIOSCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.ios"));
	if (ToBuildTargetPlatform(m_targetPlatform) != EBuildTargetPlatform::IOS)
	{
		ImGui::TextDisabled("%s", Loc::Text("build_settings.platform_inactive"));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_ios", 4.0f, { 2.0f, 1.0f }, 170.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.ios_bundle_identifier.desc"));
			label(Loc::Text("build_settings.ios_bundle_identifier"));
		},
		[&]() {
			if (ImGui::InputText("##build.ios_bundle_identifier", &m_iosBundleIdentifier))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.ios_team_id.desc"));
			label(Loc::Text("build_settings.ios_team_id"));
		},
		[&]() {
			if (ImGui::InputText("##build.ios_team_id", &m_iosTeamId))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.ios_minimum_os.desc"));
			label(Loc::Text("build_settings.ios_minimum_os"));
		},
		[&]() {
			if (ImGui::InputText("##build.ios_minimum_os", &m_iosMinimumOSVersion))
			{
				MarkDirty();
			}
		});
}

void CBuildSettingsWindow::DrawWindowsIconSelector()
{
	const AssetMetaData* iconMeta = FindWindowsIconMeta();
	const bool hasIcon = nullptr != iconMeta && false == m_windowsIconGuid.IsNull();
	const char* caption = hasIcon ? "ICO" : "+";
	const ImVec2 buttonSize(72.0f, 72.0f);
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const std::string id = std::string("##build.windows_icon_button");

	ImGui::InvisibleButton(id.c_str(), buttonSize);
	const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 bg = ImGui::GetColorU32(hasIcon ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg);
	const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
	drawList->AddRectFilled(start, ImVec2(start.x + buttonSize.x, start.y + buttonSize.y), bg, 4.0f);
	drawList->AddRect(start, ImVec2(start.x + buttonSize.x, start.y + buttonSize.y), border, 4.0f);
	const ImVec2 textSize = ImGui::CalcTextSize(caption);
	drawList->AddText(
		ImVec2(start.x + (buttonSize.x - textSize.x) * 0.5f, start.y + (buttonSize.y - textSize.y) * 0.5f),
		ImGui::GetColorU32(ImGuiCol_Text),
		caption);
	ImGui::Utillity::HoveredToolTip(Loc::Text("build_settings.windows_icon.desc"));

	if (clicked)
	{
		std::string error;
		if (SelectWindowsIcon(&error))
		{
			MarkDirty();
		}
		else if (false == error.empty())
		{
			m_errorMessage = error;
		}
	}

	ImGui::SameLine();
	ImGui::BeginGroup();
	if (hasIcon)
	{
		ImGui::TextWrapped("%s", ToUtf8PathString(iconMeta->Path).c_str());
		ImGui::TextDisabled("%s", m_windowsIconGuid.generic_string().c_str());
		if (ImGui::SmallButton(Loc::Text("common.delete")))
		{
			m_windowsIconGuid = INVALID_ASSET_GUID;
			MarkDirty();
		}
	}
	else
	{
		ImGui::TextDisabled("%s", Loc::Text("build_settings.windows_icon.empty"));
	}
	ImGui::EndGroup();
}

void CBuildSettingsWindow::DrawScenesCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.scenes"));

	ImGui::Utillity::FormLayout layout("##build_settings_scenes", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.startup_scene.desc"));
			label(Loc::Text("build_settings.startup_scene"));
		},
		[&]() {
			std::string selectedPath = m_startupScene;
			const std::wstring title = Utillity::U8ToWString(Loc::Text("build_settings.select_startup_scene"));
			if (DrawReadOnlyPathWithFileBrowse(
				"##build.startup_scene",
				selectedPath,
				"##build.startup_scene.browse",
				title.c_str(),
				GetAssetDialogPath(),
				{ { L"JBro Scene", L"*.JScene" }, { L"All Files", L"*.*" } }))
			{
				SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
				m_startupScene = NormalizePathForProject(selectedPath, pm ? pm->GetAssetPath() : File::Path());
				if (false == m_startupScene.empty()
					&& std::find(m_buildScenes.begin(), m_buildScenes.end(), m_startupScene) == m_buildScenes.end())
				{
					m_buildScenes.insert(m_buildScenes.begin(), m_startupScene);
				}
				MarkDirty();
			}
		});

	ImGui::Spacing();
	if (ImGui::Button(Loc::Text("build_settings.use_current_scene")))
	{
		SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
		const File::Path& activeScenePath = Editor::GetActiveScenePath();
		if (pm && false == activeScenePath.empty())
		{
			m_startupScene = NormalizePathForProject(activeScenePath, pm->GetAssetPath());
			if (false == m_startupScene.empty()
				&& std::find(m_buildScenes.begin(), m_buildScenes.end(), m_startupScene) == m_buildScenes.end())
			{
				m_buildScenes.insert(m_buildScenes.begin(), m_startupScene);
			}
			MarkDirty();
		}
	}
	ImGui::Utillity::HoveredToolTip(Loc::Text("build_settings.use_current_scene.desc"));

	ImGui::Spacing();
	ImGui::SeparatorText(Loc::Text("build_settings.build_scenes"));
	ImGui::BeginChild("##build_scenes", ImVec2(0.0f, 150.0f), true);
	for (std::size_t i = 0; i < m_buildScenes.size();)
	{
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(m_buildScenes[i].c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 64.0f);
		if (ImGui::SmallButton(Loc::Text("common.delete")))
		{
			m_buildScenes.erase(m_buildScenes.begin() + static_cast<std::ptrdiff_t>(i));
			MarkDirty();
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++i;
	}
	ImGui::EndChild();

	std::vector<File::Path> selectedScenes;
	const std::wstring addSceneTitle = Utillity::U8ToWString(Loc::Text("build_settings.add_scene"));
	if (ImGui::Utillity::BrowseFilesButton(
		Loc::Text("build_settings.add_scene"),
		selectedScenes,
		addSceneTitle.c_str(),
		GetAssetDialogPath().c_str(),
		{ { L"JBro Scene", L"*.JScene" }, { L"All Files", L"*.*" } }))
	{
		SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
		const File::Path assetPath = pm ? pm->GetAssetPath() : File::Path();
		for (const File::Path& selectedScene : selectedScenes)
		{
			std::string scene = NormalizePathForProject(selectedScene, assetPath);
			if (false == scene.empty()
				&& std::find(m_buildScenes.begin(), m_buildScenes.end(), scene) == m_buildScenes.end())
			{
				m_buildScenes.push_back(std::move(scene));
				MarkDirty();
			}
		}
	}
}

void CBuildSettingsWindow::DrawOutputCategory()
{
	ImGui::SeparatorText(Loc::Text("build_settings.output"));

	ImGui::Utillity::FormLayout layout("##build_settings_output", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.output_directory.desc"));
			label(Loc::Text("build_settings.output_directory"));
		},
		[&]() {
			std::string selectedPath = m_outputDirectory;
			const std::wstring title = Utillity::U8ToWString(Loc::Text("build_settings.select_output_directory"));
			DrawReadOnlyPathWithFolderBrowse(
				"##build.output_directory",
				selectedPath,
				"##build.output_directory.browse",
				title.c_str(),
				GetRootDialogPath());
			if (selectedPath != m_outputDirectory)
			{
				SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
				m_outputDirectory = NormalizePathForProject(selectedPath, pm ? pm->GetRootPath() : File::Path());
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("build_settings.package_preview.desc"));
			label(Loc::Text("build_settings.package_preview"));
		},
		[&]() {
			const std::string preview = MakePackagePreview();
			ImGui::TextWrapped("%s", preview.c_str());
		});
}

void CBuildSettingsWindow::DrawFooterButtons()
{
	if (false == m_errorMessage.empty())
	{
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", m_errorMessage.c_str());
	}

	const bool applied = ImGui::Button(Loc::Text("common.apply"), { 100.0f, 0.0f });
	ImGui::Utillity::HoveredToolTip(Loc::Text("build_settings.apply.desc"));
	if (applied)
	{
		std::string error;
		if (SaveEditsToProject(&error))
		{
			SetVisible(false);
		}
		else
		{
			m_errorMessage = false == error.empty() ? error : Loc::Text("build_settings.save_failed");
		}
	}

	ImGui::SameLine();
	const bool cancelled = ImGui::Button(Loc::Text("common.cancel"), { 100.0f, 0.0f });
	ImGui::Utillity::HoveredToolTip(Loc::Text("build_settings.cancel.desc"));
	if (cancelled)
	{
		LoadFromProject();
		SetVisible(false);
	}
}

void CBuildSettingsWindow::LoadFromProject()
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	if (false == pm.IsValid())
	{
		return;
	}

	const ProjectBuildSettings& build = pm->GetBuildSettings();
	m_productName = build.ProductName;
	m_targetPlatform = ToIndex(build.TargetPlatform);
	m_buildConfiguration = ToIndex(build.BuildConfiguration);
	m_outputDirectory = build.OutputDirectory;
	m_startupScene = build.StartupScene;
	m_buildScenes = build.BuildScenes;
	m_windowsIconGuid = build.WindowsIconGuid;
	m_androidApplicationId = build.AndroidApplicationId;
	m_androidMinSdkVersion = static_cast<int>(build.AndroidMinSdkVersion);
	m_androidTargetSdkVersion = static_cast<int>(build.AndroidTargetSdkVersion);
	m_androidAbi = build.AndroidAbi;
	m_iosBundleIdentifier = build.IOSBundleIdentifier;
	m_iosTeamId = build.IOSTeamId;
	m_iosMinimumOSVersion = build.IOSMinimumOSVersion;
	m_loadedFromProject = true;
	m_dirty = false;
	m_errorMessage.clear();
}

bool CBuildSettingsWindow::ApplyToProject(std::string* outError)
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	if (false == pm.IsValid())
	{
		if (outError) *outError = Loc::Text("build_settings.no_project");
		return false;
	}

	const ProjectBuildSettings previousSettings = pm->GetBuildSettings();
	ProjectBuildSettings buildSettings = previousSettings;
	buildSettings.ProductName = m_productName;
	buildSettings.TargetPlatform = ToBuildTargetPlatform(m_targetPlatform);
	buildSettings.BuildConfiguration = ToBuildConfiguration(m_buildConfiguration);
	buildSettings.OutputDirectory = m_outputDirectory;
	buildSettings.StartupScene = m_startupScene;
	buildSettings.BuildScenes = m_buildScenes;
	buildSettings.WindowsIconGuid = m_windowsIconGuid;
	buildSettings.AndroidApplicationId = m_androidApplicationId;
	buildSettings.AndroidMinSdkVersion = static_cast<std::uint32_t>(std::max(1, m_androidMinSdkVersion));
	buildSettings.AndroidTargetSdkVersion = static_cast<std::uint32_t>(std::max(1, m_androidTargetSdkVersion));
	buildSettings.AndroidAbi = m_androidAbi;
	buildSettings.IOSBundleIdentifier = m_iosBundleIdentifier;
	buildSettings.IOSTeamId = m_iosTeamId;
	buildSettings.IOSMinimumOSVersion = m_iosMinimumOSVersion;

	if (buildSettings.TargetPlatform == EBuildTargetPlatform::Windows)
	{
		buildSettings.ScriptMode = EBuildScriptMode::DynamicLibrary;
		buildSettings.ScriptProjectPath = "Contents/GameScript.vcxproj";
		buildSettings.ScriptOutputLibraryPath = "GameScript.dll";
	}
	else
	{
		buildSettings.ScriptMode = EBuildScriptMode::Static;
		buildSettings.ScriptProjectPath.clear();
		buildSettings.ScriptOutputLibraryPath.clear();
	}
	buildSettings.ScriptBuildConfiguration = EBuildConfiguration::Debug == buildSettings.BuildConfiguration
		? EScriptBuildConfiguration::Debug
		: EScriptBuildConfiguration::Release;

	pm->SetBuildSettings(buildSettings);
	if (false == pm->SaveProject(outError))
	{
		pm->SetBuildSettings(previousSettings);
		return false;
	}
	LoadFromProject();
	return true;
}

bool CBuildSettingsWindow::SelectWindowsIcon(std::string* outError)
{
	File::Path selectedPath;
	const std::wstring title = Utillity::U8ToWString(Loc::Text("build_settings.select_windows_icon"));
	if (false == File::ShowOpenFileDialog(
		ImGui::Utillity::GetDialogOwnerHandle(),
		title.c_str(),
		GetAssetDialogPath().c_str(),
		{ { L"Windows Icon", L"*.ico" } },
		selectedPath))
	{
		return false;
	}

	AssetGuid selectedGuid = INVALID_ASSET_GUID;
	if (false == ImportWindowsIconAsset(selectedPath, selectedGuid, outError))
	{
		return false;
	}

	m_windowsIconGuid = selectedGuid;
	return true;
}

bool CBuildSettingsWindow::ImportWindowsIconAsset(const File::Path& selectedPath, AssetGuid& outGuid, std::string* outError) const
{
	outGuid = INVALID_ASSET_GUID;
	if (false == HasIcoExtension(selectedPath))
	{
		if (outError) *outError = Loc::Text("build_settings.windows_icon.invalid");
		return false;
	}

	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	SafePtr<IAssetManager> assetManager = Engine.AssetManager;
	if (false == pm.IsValid() || false == assetManager.IsValid())
	{
		if (outError) *outError = Loc::Text("build_settings.no_project");
		return false;
	}

	File::Path relativeAssetPath;
	if (false == TryMakeRelativeSubPath(selectedPath, pm->GetAssetPath(), relativeAssetPath))
	{
		const File::Path targetPath = pm->GetAssetPath() / WINDOWS_ICON_ASSET_PATH;
		std::error_code ec;
		std::filesystem::create_directories(targetPath.parent_path(), ec);
		if (ec)
		{
			if (outError) *outError = ec.message();
			return false;
		}

		const File::Path absoluteSelected = std::filesystem::absolute(selectedPath, ec);
		ec.clear();
		const File::Path absoluteTarget = std::filesystem::absolute(targetPath, ec);
		if (absoluteSelected != absoluteTarget)
		{
			std::filesystem::copy_file(selectedPath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				if (outError) *outError = ec.message();
				return false;
			}
		}
		relativeAssetPath = WINDOWS_ICON_ASSET_PATH;
	}

	AssetImportDesc importDesc;
	importDesc.Type = EAssetType::Custom;
	importDesc.Path = File::Path(relativeAssetPath.generic_string());
	importDesc.DisplayName = "AppIcon";
	importDesc.Importer = "WindowsIcon";

	AssetMetaData metaData;
	if (false == assetManager->ImportAsset(importDesc, &metaData))
	{
		if (outError) *outError = Loc::Text("build_settings.windows_icon.import_failed");
		return false;
	}
	assetManager->RefreshAssetRegistry();

	const AssetMetaData* registeredMeta = assetManager->GetRegistry().FindAssetByPath(importDesc.Path);
	outGuid = registeredMeta ? registeredMeta->Guid : metaData.Guid;
	if (outGuid.IsNull())
	{
		if (outError) *outError = Loc::Text("build_settings.windows_icon.import_failed");
		return false;
	}
	return true;
}

const AssetMetaData* CBuildSettingsWindow::FindWindowsIconMeta() const
{
	if (m_windowsIconGuid.IsNull() || false == Engine.AssetManager.IsValid())
	{
		return nullptr;
	}
	return Engine.AssetManager->GetRegistry().FindAsset(m_windowsIconGuid);
}

bool CBuildSettingsWindow::HasUnsavedChanges() const
{
	return m_dirty;
}

bool CBuildSettingsWindow::SaveEditsToProject(std::string* outError)
{
	if (false == m_loadedFromProject)
	{
		LoadFromProject();
		return true;
	}

	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	if (false == pm.IsValid() || false == pm->IsProjectLoaded())
	{
		if (outError) *outError = Loc::Text("build_settings.no_project");
		return false;
	}

	return ApplyToProject(outError);
}

void CBuildSettingsWindow::MarkDirty()
{
	m_dirty = true;
	m_errorMessage.clear();
}

std::string CBuildSettingsWindow::NormalizePathForProject(const std::string& selectedPath, const File::Path& basePath) const
{
	if (selectedPath.empty())
	{
		return {};
	}

	return NormalizePathForProject(File::Path(Utillity::U8ToWString(selectedPath)), basePath);
}

std::string CBuildSettingsWindow::NormalizePathForProject(const File::Path& selectedPath, const File::Path& basePath) const
{
	if (selectedPath.empty())
	{
		return {};
	}

	if (false == basePath.empty())
	{
		std::error_code ec;
		const File::Path relativePath = std::filesystem::relative(selectedPath, basePath, ec);
		if (false == static_cast<bool>(ec) && IsSubPath(relativePath))
		{
			return ToUtf8PathString(relativePath);
		}
	}

	return ToUtf8PathString(selectedPath);
}

std::string CBuildSettingsWindow::MakePackagePreview() const
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	File::Path outputRoot = m_outputDirectory.empty()
		? File::Path("Dist/Games")
		: File::Path(Utillity::U8ToWString(m_outputDirectory));
	if (pm && false == outputRoot.is_absolute())
	{
		outputRoot = pm->GetRootPath() / outputRoot;
	}

	std::string product = m_productName.empty() ? "Game" : m_productName;
	const File::Path packagePath = outputRoot / (product + "-" + ToString(ToBuildTargetPlatform(m_targetPlatform)) + "-" + ToString(ToBuildConfiguration(m_buildConfiguration)));
	return ToUtf8PathString(packagePath);
}

std::wstring CBuildSettingsWindow::GetRootDialogPath() const
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	return pm ? pm->GetRootPath().wstring() : std::wstring();
}

std::wstring CBuildSettingsWindow::GetAssetDialogPath() const
{
	SafePtr<CProjectManager> pm = GetProjectManagerForBuildSettings();
	return pm ? pm->GetAssetPath().wstring() : std::wstring();
}
