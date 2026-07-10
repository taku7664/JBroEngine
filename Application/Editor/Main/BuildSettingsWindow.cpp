#include "pch.h"
#include "BuildSettingsWindow.h"

#include "Editor/Editor.h"
#include "Editor/Build/BuildSettingsUtils.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Editor/EditorContext.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImAssetField.h"
#include "Editor/ImItem/ImSplitter.h"
#include "Editor/ImItem/ImText.h"
#include "Engine/Editor/ImWindow/ImWindowFlag.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Utillity/String/StringUtillity.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

namespace
{
	const File::Path WINDOWS_ICON_ASSET_PATH = "Package/Windows/AppIcon.ico";
	const ImVec4 REQUIRED_FIELD_COLOR(0.95f, 0.35f, 0.30f, 1.0f);

	int ToIndex(EBuildConfiguration configuration)
	{
		return EBuildConfiguration::Debug == configuration ? 0 : 1;
	}

	EBuildConfiguration ToBuildConfiguration(int index)
	{
		return 0 == index ? EBuildConfiguration::Debug : EBuildConfiguration::Release;
	}

	bool HasIcoExtension(const File::Path& path)
	{
		std::wstring extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
			return static_cast<wchar_t>(std::towlower(ch));
		});
		return extension == L".ico";
	}

	bool HasExtensionIgnoreCase(const std::string& value, const wchar_t* expectedExtension)
	{
		if (BuildSettingsUtils::IsBlank(value))
		{
			return false;
		}

		std::wstring extension = File::Path(Utillity::U8ToWString(value)).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
			return static_cast<wchar_t>(std::towlower(ch));
		});
		return extension == expectedExtension;
	}

	void PushInvalidInputStyle(bool invalid)
	{
		if (false == invalid)
		{
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Border, REQUIRED_FIELD_COLOR);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
	}

	void PopInvalidInputStyle(bool invalid)
	{
		if (false == invalid)
		{
			return;
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	void DrawFieldLabel(const char* labelKey, const char* tooltipKey, bool required)
	{
		ImFieldLabel(Loc::Text(labelKey))
			.Tooltip(Loc::Text(tooltipKey))
			.Required(required)
			.Draw();
	}

	bool DrawInputTextWithInvalid(const char* inputId, std::string& value, bool invalid)
	{
		ImInputText input(inputId);
		input.SetText(value);
		if (input(ImGuiInputTextFlags_None, invalid))
		{
			value = input.GetString();
			return true;
		}
		return false;
	}

	std::string MakeBrowseId(const char* suffix)
	{
		std::string id = Loc::Text(EditorLocKeys::CommonBrowse);
		id += suffix;
		return id;
	}

	bool DrawReadOnlyPathWithFolderBrowse(
		const char* inputId,
		std::string& value,
		const char* buttonSuffix,
		const wchar_t* title,
		const std::wstring& initialDirectory,
		bool invalid = false)
	{
		const std::string browseId = MakeBrowseId(buttonSuffix);
		const float buttonWidth = ImGui::CalcTextSize(Loc::Text(EditorLocKeys::CommonBrowse)).x
			+ ImGui::GetStyle().FramePadding.x * 2.0f
			+ 8.0f;

		ImPathField(inputId, value)
			.ReadOnly(true)
			.Invalid(invalid)
			.ReserveTrailingWidth(buttonWidth)
			.Draw();
		ImGui::SameLine();
		return ImGui::Utillity::BrowseFolderButton(browseId.c_str(), value, title, initialDirectory.c_str());
	}

	bool DrawReadOnlyPathWithFileBrowse(
		const char* inputId,
		std::string& value,
		const char* buttonSuffix,
		const wchar_t* title,
		const std::wstring& initialDirectory,
		std::vector<File::FileDialogFilter> filters,
		bool invalid = false)
	{
		const std::string browseId = MakeBrowseId(buttonSuffix);
		const float buttonWidth = ImGui::CalcTextSize(Loc::Text(EditorLocKeys::CommonBrowse)).x
			+ ImGui::GetStyle().FramePadding.x * 2.0f
			+ 8.0f;

		ImPathField(inputId, value)
			.ReadOnly(true)
			.Invalid(invalid)
			.ReserveTrailingWidth(buttonWidth)
			.Draw();
		ImGui::SameLine();
		return ImGui::Utillity::BrowseFileButton(
			browseId.c_str(), value, title, initialDirectory.c_str(), std::move(filters));
	}
}

void CBuildSettingsWindow::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowBuildSettings);

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
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid() || false == pm->IsProjectLoaded())
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsNoProject));
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
	struct CommonEntry { ECategory Kind; const char* LocKey; };
	static const CommonEntry commonEntry[] = {
		{ ECategory::General, EditorLocKeys::BuildSettingsCategoryGeneral },
		{ ECategory::Scenes,  EditorLocKeys::BuildSettingsCategoryScenes  },
		{ ECategory::Output,  EditorLocKeys::BuildSettingsCategoryOutput  },
	};
	struct PlatformEntry { ECategory Kind; EBuildTargetPlatform Platform; const char* LocKey; };
	static const PlatformEntry platformEntry[] = {
		{ ECategory::Windows, EBuildTargetPlatform::Windows, EditorLocKeys::BuildSettingsCategoryWindows },
		{ ECategory::Web,     EBuildTargetPlatform::Web,     EditorLocKeys::BuildSettingsCategoryWeb     },
		{ ECategory::Android, EBuildTargetPlatform::Android, EditorLocKeys::BuildSettingsCategoryAndroid },
		{ ECategory::IOS,     EBuildTargetPlatform::IOS,     EditorLocKeys::BuildSettingsCategoryIos     },
	};

	// 공통 카테고리(General/Scenes/Output)는 플랫폼 활성화 개념이 없으므로 체크박스 없이 나열한다.
	ImText header;
	header.UseSeparator(true);
	header.SetHoveredTooltip(Loc::Text(EditorLocKeys::BuildSettingsCommonCategoryTooltip));
	header(Loc::Text(EditorLocKeys::BuildSettingsCommonCategories));
	for (const CommonEntry& entry : commonEntry)
	{
		ImGui::Utillity::StyleBuilder style;
		if (HasCategoryInvalid(entry.Kind))
		{
			style.PushStyleColor(ImGuiCol_Text, REQUIRED_FIELD_COLOR);
		}
		if (ImGui::Selectable(Loc::Text(entry.LocKey), entry.Kind == m_selectedCategory))
		{
			m_selectedCategory = entry.Kind;
		}
	}

	// 플랫폼 카테고리는 항목 앞에 활성화 체크박스를 둔다(활성 플랫폼만 빌드/일괄빌드 대상).
	header.SetHoveredTooltip(Loc::Text(EditorLocKeys::BuildSettingsPlatformCategoryTooltip));
	header(Loc::Text(EditorLocKeys::BuildSettingsPlatformCategories));
	for (const PlatformEntry& entry : platformEntry)
	{
		ImGui::Utillity::IDGroup idGroup(entry.LocKey); // 체크박스/Selectable id 충돌 방지.
		DrawPlatformEnableCheckbox(entry.Platform);
		ImGui::SameLine();

		ImGui::Utillity::StyleBuilder style;
		if (HasCategoryInvalid(entry.Kind))
		{
			style.PushStyleColor(ImGuiCol_Text, REQUIRED_FIELD_COLOR);
		}
		if (ImGui::Selectable(Loc::Text(entry.LocKey), entry.Kind == m_selectedCategory))
		{
			m_selectedCategory = entry.Kind;
		}
	}
}

bool* CBuildSettingsWindow::PlatformEnableFlag(EBuildTargetPlatform platform)
{
	switch (platform)
	{
	case EBuildTargetPlatform::Web:     return &m_enableWeb;
	case EBuildTargetPlatform::Android: return &m_enableAndroid;
	case EBuildTargetPlatform::IOS:     return &m_enableIOS;
	case EBuildTargetPlatform::Windows:
	default:                            return &m_enableWindows;
	}
}

void CBuildSettingsWindow::DrawPlatformEnableCheckbox(EBuildTargetPlatform platform)
{
	bool* flag = PlatformEnableFlag(platform);
	if (ImGui::Checkbox("##platform_enable", flag))
	{
		MarkDirty();
	}
	ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::BuildSettingsPlatformEnableTooltip));
}

void CBuildSettingsWindow::DrawCategoryContent(float)
{
	switch (m_selectedCategory)
	{
	case ECategory::General: DrawGeneralCategory(); break;
	case ECategory::Scenes:  DrawScenesCategory();  break;
	case ECategory::Output:  DrawOutputCategory();  break;
	case ECategory::Windows: DrawWindowsCategory(); break;
	case ECategory::Web:     DrawWebCategory();     break;
	case ECategory::Android: DrawAndroidCategory(); break;
	case ECategory::IOS:     DrawIOSCategory();     break;
	default: break;
	}
}

void CBuildSettingsWindow::DrawGeneralCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsGeneral)).Draw();

	ImGui::Utillity::FormLayout layout("##build_settings_general", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsProductName, EditorLocKeys::BuildSettingsProductNameDesc, true);
		},
		[&]() {
			if (DrawInputTextWithInvalid("##build.product_name", m_productName, IsProductNameInvalid()))
			{
				MarkDirty();
			}
		});

	// 대상 플랫폼은 더 이상 단일 선택이 아니다. 좌측 플랫폼 카테고리의 활성화 체크박스로 고른다.
	const char* configurations[] = { "Debug", "Release" };
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsConfiguration, EditorLocKeys::BuildSettingsConfigurationDesc, false);
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
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsWindows)).Draw();
	if (false == m_enableWindows)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsPlatformInactive));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_windows", 4.0f, { 2.0f, 1.0f }, 170.0f);
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsWindowsIcon, EditorLocKeys::BuildSettingsWindowsIconDesc, false);
		},
		[&]() {
			DrawWindowsIconSelector();
		});
}

void CBuildSettingsWindow::DrawWebCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsWeb)).Draw();
	if (false == m_enableWeb)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsPlatformInactive));
	}

	// Web 빌드는 현재 플랫폼 전용 필수 설정이 없다(공통 설정만으로 빌드 가능).
	ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::BuildSettingsWebNoExtraSettings));
}

void CBuildSettingsWindow::DrawAndroidCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsAndroid)).Draw();
	if (false == m_enableAndroid)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsPlatformInactive));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_android", 4.0f, { 2.0f, 1.0f }, 170.0f);
	const bool validateAndroid = m_enableAndroid;
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsAndroidApplicationId, EditorLocKeys::BuildSettingsAndroidApplicationIdDesc, true);
		},
		[&]() {
			if (DrawInputTextWithInvalid("##build.android_application_id", m_androidApplicationId, validateAndroid && IsAndroidApplicationIdInvalid()))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsAndroidMinSdk, EditorLocKeys::BuildSettingsAndroidMinSdkDesc, true);
		},
		[&]() {
			const bool invalid = validateAndroid && m_androidMinSdkVersion <= 0;
			PushInvalidInputStyle(invalid);
			if (ImGui::InputInt("##build.android_min_sdk", &m_androidMinSdkVersion))
			{
				MarkDirty();
			}
			PopInvalidInputStyle(invalid);
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsAndroidTargetSdk, EditorLocKeys::BuildSettingsAndroidTargetSdkDesc, true);
		},
		[&]() {
			const bool invalid = validateAndroid && IsAndroidSdkInvalid();
			PushInvalidInputStyle(invalid);
			if (ImGui::InputInt("##build.android_target_sdk", &m_androidTargetSdkVersion))
			{
				MarkDirty();
			}
			PopInvalidInputStyle(invalid);
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsAndroidAbi, EditorLocKeys::BuildSettingsAndroidAbiDesc, false);
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

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsAndroidOrientation, EditorLocKeys::BuildSettingsAndroidOrientationDesc, false);
		},
		[&]() {
			// 회전 보정의 권위 신호. Landscape/Portrait 는 방향고정, Auto 는 기기 회전 추종.
			const char* orientations[] = { "Landscape", "Portrait", "Auto" };
			int current = 0;
			if (m_androidOrientation == "Portrait") current = 1;
			else if (m_androidOrientation == "Auto") current = 2;
			if (ImGui::Combo("##build.android_orientation", &current, orientations, IM_ARRAYSIZE(orientations)))
			{
				m_androidOrientation = orientations[current];
				MarkDirty();
			}
		});
}

void CBuildSettingsWindow::DrawIOSCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsIos)).Draw();
	if (false == m_enableIOS)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsPlatformInactive));
	}

	ImGui::Utillity::FormLayout layout("##build_settings_ios", 4.0f, { 2.0f, 1.0f }, 170.0f);
	const bool validateIOS = m_enableIOS;
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsIosBundleIdentifier, EditorLocKeys::BuildSettingsIosBundleIdentifierDesc, true);
		},
		[&]() {
			if (DrawInputTextWithInvalid("##build.ios_bundle_identifier", m_iosBundleIdentifier, validateIOS && IsIOSBundleIdentifierInvalid()))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsIosTeamId, EditorLocKeys::BuildSettingsIosTeamIdDesc, false);
		},
		[&]() {
			if (DrawInputTextWithInvalid("##build.ios_team_id", m_iosTeamId, false))
			{
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsIosMinimumOs, EditorLocKeys::BuildSettingsIosMinimumOsDesc, true);
		},
		[&]() {
			if (DrawInputTextWithInvalid("##build.ios_minimum_os", m_iosMinimumOSVersion, validateIOS && IsIOSMinimumOSInvalid()))
			{
				MarkDirty();
			}
		});
}

void CBuildSettingsWindow::DrawWindowsIconSelector()
{
	AssetMetaData iconMeta;
	const bool hasIcon = TryGetWindowsIconMeta(iconMeta) && false == m_windowsIconGuid.IsNull();
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
	ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::BuildSettingsWindowsIconDesc));

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
		ImGui::TextWrapped("%s", EditorPathUtils::ToUtf8(iconMeta.Path).c_str());
		ImGui::TextDisabled("%s", m_windowsIconGuid.generic_string().c_str());
		if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonDelete)))
		{
			m_windowsIconGuid = INVALID_ASSET_GUID;
			MarkDirty();
		}
	}
	else
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::BuildSettingsWindowsIconEmpty));
	}
	ImGui::EndGroup();
}

void CBuildSettingsWindow::DrawScenesCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsScenes)).Draw();

	ImGui::Utillity::FormLayout layout("##build_settings_scenes", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsStartupScene, EditorLocKeys::BuildSettingsStartupSceneDesc, true);
		},
		[&]() {
			std::string selectedPath = m_startupScene;
			const std::wstring title = Utillity::U8ToWString(Loc::Text(EditorLocKeys::BuildSettingsSelectStartupScene));
			if (DrawReadOnlyPathWithFileBrowse(
				"##build.startup_scene",
				selectedPath,
				"##build.startup_scene.browse",
				title.c_str(),
				GetAssetDialogPath(),
				{ { L"JBro Scene", L"*.JScene" }, { L"All Files", L"*.*" } },
				IsStartupSceneInvalid()))
			{
				SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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
	if (ImGui::Button(Loc::Text(EditorLocKeys::BuildSettingsUseCurrentScene)))
	{
		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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
	ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::BuildSettingsUseCurrentSceneDesc));

	ImGui::Spacing();
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsBuildScenes)).SpacingBefore(true).Draw();
	ImGui::BeginChild("##build_scenes", ImVec2(0.0f, 150.0f), true);
	for (std::size_t i = 0; i < m_buildScenes.size();)
	{
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(m_buildScenes[i].c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 64.0f);
		if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonDelete)))
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
	const std::wstring addSceneTitle = Utillity::U8ToWString(Loc::Text(EditorLocKeys::BuildSettingsAddScene));
	if (ImGui::Utillity::BrowseFilesButton(
		Loc::Text(EditorLocKeys::BuildSettingsAddScene),
		selectedScenes,
		addSceneTitle.c_str(),
		GetAssetDialogPath().c_str(),
		{ { L"JBro Scene", L"*.JScene" }, { L"All Files", L"*.*" } }))
	{
		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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

	// Always Include — 어떤 씬도 참조하지 않지만 런타임에 직접 로드할 자산.
	// 참조 기반 패키징에서 이 목록이 코드 로드 자산의 누락 방지 장치가 된다.
	ImGui::Spacing();
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsAlwaysInclude))
		.Description(Loc::Text(EditorLocKeys::BuildSettingsAlwaysIncludeDesc))
		.SpacingBefore(true)
		.Draw();
	for (std::size_t index = 0; index < m_alwaysIncludeAssets.size(); ++index)
	{
		ImGui::PushID(static_cast<int>(1000 + index));
		AssetGuid previousGuid = m_alwaysIncludeAssets[index];
		ImAssetField("##always_include", m_alwaysIncludeAssets[index]).Draw();
		if (m_alwaysIncludeAssets[index] != previousGuid)
		{
			MarkDirty();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonRemove)))
		{
			m_alwaysIncludeAssets.erase(m_alwaysIncludeAssets.begin() + static_cast<std::ptrdiff_t>(index));
			MarkDirty();
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	if (ImGui::Button(Loc::Text(EditorLocKeys::BuildSettingsAddAlwaysInclude)))
	{
		m_alwaysIncludeAssets.push_back(INVALID_ASSET_GUID);
		MarkDirty();
	}
}

void CBuildSettingsWindow::DrawOutputCategory()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::BuildSettingsOutput)).Draw();

	ImGui::Utillity::FormLayout layout("##build_settings_output", 4.0f, { 2.0f, 1.0f }, 150.0f);
	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsOutputDirectory, EditorLocKeys::BuildSettingsOutputDirectoryDesc, true);
		},
		[&]() {
			std::string selectedPath = m_outputDirectory;
			const std::wstring title = Utillity::U8ToWString(Loc::Text(EditorLocKeys::BuildSettingsSelectOutputDirectory));
			DrawReadOnlyPathWithFolderBrowse(
				"##build.output_directory",
				selectedPath,
				"##build.output_directory.browse",
				title.c_str(),
				GetRootDialogPath(),
				IsOutputDirectoryInvalid());
			if (selectedPath != m_outputDirectory)
			{
				SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
				m_outputDirectory = NormalizePathForProject(selectedPath, pm ? pm->GetRootPath() : File::Path());
				MarkDirty();
			}
		});

	layout.Row(
		[&]() {
			DrawFieldLabel(EditorLocKeys::BuildSettingsPackagePreview, EditorLocKeys::BuildSettingsPackagePreviewDesc, false);
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
		ImValidationMessage(m_errorMessage.c_str(), EImValidationSeverity::Error).Draw();
	}

	const bool applied = ImActionButton(Loc::Text(EditorLocKeys::CommonApply))
		.Severity(EImValidationSeverity::Success)
		.Size({ 100.0f, 0.0f })
		.Tooltip(EditorSimulationGuard::CanSaveProject()
			? Loc::Text(EditorLocKeys::BuildSettingsApplyDesc)
			: EditorSimulationGuard::GetSaveBlockedMessage())
		.Disabled(false == EditorSimulationGuard::CanSaveProject())
		.Draw();
	if (applied)
	{
		std::string error;
		if (SaveEditsToProject(&error))
		{
			SetVisible(false);
		}
		else
		{
			m_errorMessage = false == error.empty() ? error : Loc::Text(EditorLocKeys::BuildSettingsSaveFailed);
		}
	}

	ImGui::SameLine();
	const bool cancelled = ImActionButton(Loc::Text(EditorLocKeys::CommonCancel))
		.Size({ 100.0f, 0.0f })
		.Tooltip(Loc::Text(EditorLocKeys::BuildSettingsCancelDesc))
		.Draw();
	if (cancelled)
	{
		LoadFromProject();
		SetVisible(false);
	}
}

void CBuildSettingsWindow::LoadFromProject()
{
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid())
	{
		return;
	}

	const ProjectBuildSettings& build = pm->GetBuildSettings();
	m_productName = build.ProductName;
	m_enableWindows = build.EnableWindows;
	m_enableWeb     = build.EnableWeb;
	m_enableAndroid = build.EnableAndroid;
	m_enableIOS     = build.EnableIOS;
	m_buildConfiguration = ToIndex(build.BuildConfiguration);
	m_outputDirectory = build.OutputDirectory;
	m_startupScene = build.StartupScene;
	m_buildScenes = build.BuildScenes;
	m_alwaysIncludeAssets = build.AlwaysIncludeAssets;
	m_windowsIconGuid = build.WindowsIconGuid;
	m_androidApplicationId = build.AndroidApplicationId;
	m_androidMinSdkVersion = static_cast<int>(build.AndroidMinSdkVersion);
	m_androidTargetSdkVersion = static_cast<int>(build.AndroidTargetSdkVersion);
	m_androidAbi = build.AndroidAbi;
	m_androidOrientation = build.AndroidOrientation;
	m_iosBundleIdentifier = build.IOSBundleIdentifier;
	m_iosTeamId = build.IOSTeamId;
	m_iosMinimumOSVersion = build.IOSMinimumOSVersion;
	m_loadedFromProject = true;
	m_dirty = false;
	m_errorMessage.clear();
}

bool CBuildSettingsWindow::ApplyToProject(std::string* outError)
{
	if (false == EditorSimulationGuard::CanSaveProject())
	{
		if (outError) *outError = EditorSimulationGuard::GetSaveBlockedMessage();
		return false;
	}

	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid())
	{
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsNoProject);
		return false;
	}

	const ProjectBuildSettings previousSettings = pm->GetBuildSettings();
	ProjectBuildSettings buildSettings = previousSettings;
	buildSettings.ProductName = m_productName;
	buildSettings.EnableWindows = m_enableWindows;
	buildSettings.EnableWeb     = m_enableWeb;
	buildSettings.EnableAndroid = m_enableAndroid;
	buildSettings.EnableIOS     = m_enableIOS;
	buildSettings.BuildConfiguration = ToBuildConfiguration(m_buildConfiguration);
	buildSettings.OutputDirectory = m_outputDirectory;
	buildSettings.StartupScene = m_startupScene;
	buildSettings.BuildScenes = m_buildScenes;
	buildSettings.AlwaysIncludeAssets = m_alwaysIncludeAssets;
	buildSettings.WindowsIconGuid = m_windowsIconGuid;
	buildSettings.AndroidApplicationId = m_androidApplicationId;
	buildSettings.AndroidMinSdkVersion = static_cast<std::uint32_t>(std::max(1, m_androidMinSdkVersion));
	buildSettings.AndroidTargetSdkVersion = static_cast<std::uint32_t>(std::max(1, m_androidTargetSdkVersion));
	buildSettings.AndroidAbi = m_androidAbi;
	buildSettings.AndroidOrientation = m_androidOrientation;
	buildSettings.IOSBundleIdentifier = m_iosBundleIdentifier;
	buildSettings.IOSTeamId = m_iosTeamId;
	buildSettings.IOSMinimumOSVersion = m_iosMinimumOSVersion;

	// ScriptMode 는 DynamicLibrary 기준으로 저장한다(Windows 빌드 기본). Web/모바일 빌드의
	// 정적 링크 강제는 빌드 디스크립터 생성 시점(CGameBuildManager)에서 플랫폼별로 처리한다 —
	// 다중 활성 설정에서는 여기서 단일 플랫폼으로 ScriptMode 를 못 정하기 때문이다.
	buildSettings.ScriptMode = EBuildScriptMode::DynamicLibrary;
	buildSettings.ScriptProjectPath = "Contents/GameScript.vcxproj";
	buildSettings.ScriptOutputLibraryPath = "GameScript.dll";
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
	const std::wstring title = Utillity::U8ToWString(Loc::Text(EditorLocKeys::BuildSettingsSelectWindowsIcon));
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
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsWindowsIconInvalid);
		return false;
	}

	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	SafePtr<IAssetManager> assetManager = Engine.AssetManager;
	if (false == pm.IsValid() || false == assetManager.IsValid())
	{
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsNoProject);
		return false;
	}

	File::Path relativeAssetPath;
	if (false == EditorPathUtils::TryMakeRelativeSubPath(selectedPath, pm->GetAssetPath(), relativeAssetPath))
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
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsWindowsIconImportFailed);
		return false;
	}
	assetManager->RefreshAssetRegistry();

	AssetMetaData registeredMeta;
	outGuid = assetManager->GetRegistry().TryGetAssetByPath(importDesc.Path, registeredMeta)
		? registeredMeta.Guid
		: metaData.Guid;
	if (outGuid.IsNull())
	{
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsWindowsIconImportFailed);
		return false;
	}
	return true;
}

bool CBuildSettingsWindow::TryGetWindowsIconMeta(AssetMetaData& outMetaData) const
{
	if (m_windowsIconGuid.IsNull() || false == Engine.AssetManager.IsValid())
	{
		return false;
	}
	return Engine.AssetManager->GetRegistry().TryGetAsset(m_windowsIconGuid, outMetaData);
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

	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	if (false == pm.IsValid() || false == pm->IsProjectLoaded())
	{
		if (outError) *outError = Loc::Text(EditorLocKeys::BuildSettingsNoProject);
		return false;
	}

	return ApplyToProject(outError);
}

void CBuildSettingsWindow::MarkDirty()
{
	m_dirty = true;
	m_errorMessage.clear();
}

void CBuildSettingsWindow::FocusFirstInvalidCategory()
{
	if (false == m_loadedFromProject)
	{
		LoadFromProject();
	}

	static const ECategory orderedCategories[] = {
		ECategory::General,
		ECategory::Scenes,
		ECategory::Output,
		ECategory::Windows,
		ECategory::Web,
		ECategory::Android,
		ECategory::IOS,
	};
	for (ECategory category : orderedCategories)
	{
		if (HasCategoryInvalid(category))
		{
			m_selectedCategory = category;
			return;
		}
	}
	m_selectedCategory = ECategory::General;
}

void CBuildSettingsWindow::FocusPlatformCategory(EBuildTargetPlatform platform)
{
	if (false == m_loadedFromProject)
	{
		LoadFromProject();
	}

	switch (platform)
	{
	case EBuildTargetPlatform::Web:     m_selectedCategory = ECategory::Web;     break;
	case EBuildTargetPlatform::Android: m_selectedCategory = ECategory::Android; break;
	case EBuildTargetPlatform::IOS:     m_selectedCategory = ECategory::IOS;     break;
	case EBuildTargetPlatform::Windows:
	default:                            m_selectedCategory = ECategory::Windows; break;
	}
}

bool CBuildSettingsWindow::HasCategoryInvalid(ECategory category) const
{
	switch (category)
	{
	case ECategory::General:
		return IsProductNameInvalid();
	case ECategory::Scenes:
		return IsStartupSceneInvalid();
	case ECategory::Output:
		return IsOutputDirectoryInvalid();
	case ECategory::Android:
		return m_enableAndroid
			&& (IsAndroidApplicationIdInvalid() || IsAndroidSdkInvalid());
	case ECategory::IOS:
		return m_enableIOS
			&& (IsIOSBundleIdentifierInvalid() || IsIOSMinimumOSInvalid());
	case ECategory::Windows:
	case ECategory::Web:
	default:
		// Windows/Web 은 현재 플랫폼 전용 필수 설정이 없다(추후 확장 여지).
		return false;
	}
}

bool CBuildSettingsWindow::IsProductNameInvalid() const
{
	return BuildSettingsUtils::IsBlank(m_productName);
}

bool CBuildSettingsWindow::IsStartupSceneInvalid() const
{
	return false == HasExtensionIgnoreCase(m_startupScene, L".jscene");
}

bool CBuildSettingsWindow::IsOutputDirectoryInvalid() const
{
	return BuildSettingsUtils::IsBlank(m_outputDirectory);
}

bool CBuildSettingsWindow::IsAndroidApplicationIdInvalid() const
{
	return false == BuildSettingsUtils::IsValidAndroidApplicationId(m_androidApplicationId);
}

bool CBuildSettingsWindow::IsAndroidSdkInvalid() const
{
	return m_androidMinSdkVersion <= 0
		|| m_androidTargetSdkVersion <= 0
		|| m_androidTargetSdkVersion < m_androidMinSdkVersion;
}

bool CBuildSettingsWindow::IsIOSBundleIdentifierInvalid() const
{
	return false == BuildSettingsUtils::IsValidIOSBundleIdentifier(m_iosBundleIdentifier);
}

bool CBuildSettingsWindow::IsIOSMinimumOSInvalid() const
{
	return false == BuildSettingsUtils::IsValidVersionString(m_iosMinimumOSVersion);
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
		if (false == static_cast<bool>(ec) && EditorPathUtils::IsRelativeSubPath(relativePath))
		{
			return EditorPathUtils::ToUtf8(relativePath);
		}
	}

	return EditorPathUtils::ToUtf8(selectedPath);
}

std::string CBuildSettingsWindow::MakePackagePreview() const
{
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	File::Path outputRoot = m_outputDirectory.empty()
		? File::Path("Dist/Games")
		: File::Path(Utillity::U8ToWString(m_outputDirectory));
	if (pm && false == outputRoot.is_absolute())
	{
		outputRoot = pm->GetRootPath() / outputRoot;
	}

	const std::string product = m_productName.empty() ? "Game" : m_productName;
	const char* config = BuildSettingsUtils::GetBuildConfigurationName(ToBuildConfiguration(m_buildConfiguration));

	// 활성 플랫폼마다 패키지 경로가 하나씩 생긴다. 활성 플랫폼별로 나열한다.
	struct PlatformFlag { bool Enabled; EBuildTargetPlatform Platform; };
	const PlatformFlag flags[] = {
		{ m_enableWindows, EBuildTargetPlatform::Windows },
		{ m_enableWeb,     EBuildTargetPlatform::Web     },
		{ m_enableAndroid, EBuildTargetPlatform::Android },
		{ m_enableIOS,     EBuildTargetPlatform::IOS     },
	};

	std::string preview;
	for (const PlatformFlag& flag : flags)
	{
		if (false == flag.Enabled)
		{
			continue;
		}
		const File::Path packagePath = outputRoot / (product + "-" + BuildSettingsUtils::GetTargetPlatformName(flag.Platform) + "-" + config);
		if (false == preview.empty())
		{
			preview += "\n";
		}
		preview += EditorPathUtils::ToUtf8(packagePath);
	}

	return preview.empty() ? std::string(Loc::Text(EditorLocKeys::BuildSettingsNoPlatformEnabled)) : preview;
}

std::wstring CBuildSettingsWindow::GetRootDialogPath() const
{
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	return pm ? pm->GetRootPath().wstring() : std::wstring();
}

std::wstring CBuildSettingsWindow::GetAssetDialogPath() const
{
	SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
	return pm ? pm->GetAssetPath().wstring() : std::wstring();
}
