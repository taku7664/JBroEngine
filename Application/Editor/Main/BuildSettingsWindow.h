#pragma once

#include "Engine/Editor/Project/ProjectTypes.h"

#include <string>
#include <vector>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImCustomWindow.h"

class CBuildSettingsWindow : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;
	virtual ~CBuildSettingsWindow() = default;

	bool HasUnsavedChanges() const;
	bool SaveEditsToProject(std::string* outError = nullptr);
	void FocusFirstInvalidCategory();

private:
	enum class ECategory
	{
		General,
		Scenes,
		Output,
		Windows,
		Android,
		IOS,
		Count
	};

	void OnCreate() override;
	void OnShow() override;
	void OnRenderStay() override;

	void DrawCategoryList(float panelWidth);
	void DrawCategoryContent(float panelWidth);
	void DrawGeneralCategory();
	void DrawScenesCategory();
	void DrawOutputCategory();
	void DrawWindowsCategory();
	void DrawAndroidCategory();
	void DrawIOSCategory();
	void DrawFooterButtons();

	void LoadFromProject();
	bool ApplyToProject(std::string* outError);
	void MarkDirty();
	bool HasCategoryInvalid(ECategory category) const;
	bool IsProductNameInvalid() const;
	bool IsStartupSceneInvalid() const;
	bool IsOutputDirectoryInvalid() const;
	bool IsAndroidApplicationIdInvalid() const;
	bool IsAndroidSdkInvalid() const;
	bool IsIOSBundleIdentifierInvalid() const;
	bool IsIOSMinimumOSInvalid() const;

	void DrawWindowsIconSelector();
	bool SelectWindowsIcon(std::string* outError);
	bool ImportWindowsIconAsset(const File::Path& selectedPath, AssetGuid& outGuid, std::string* outError) const;
	const AssetMetaData* FindWindowsIconMeta() const;

	std::string NormalizePathForProject(const std::string& selectedPath, const File::Path& basePath) const;
	std::string NormalizePathForProject(const File::Path& selectedPath, const File::Path& basePath) const;
	std::string MakePackagePreview() const;
	std::wstring GetRootDialogPath() const;
	std::wstring GetAssetDialogPath() const;

	ECategory m_selectedCategory = ECategory::General;
	float m_splitRatio = 0.28f;

	std::string m_productName;
	int m_targetPlatform = 0;
	int m_buildConfiguration = 1;
	std::string m_outputDirectory;
	std::string m_startupScene;
	std::vector<std::string> m_buildScenes;
	AssetGuid m_windowsIconGuid = INVALID_ASSET_GUID;
	std::string m_androidApplicationId;
	int m_androidMinSdkVersion = 26;
	int m_androidTargetSdkVersion = 35;
	std::string m_androidAbi;
	std::string m_iosBundleIdentifier;
	std::string m_iosTeamId;
	std::string m_iosMinimumOSVersion;
	bool m_loadedFromProject = false;
	bool m_dirty = false;
	std::string m_errorMessage;
};

#endif
