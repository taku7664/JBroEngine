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

private:
	enum class ECategory
	{
		General,
		Scenes,
		Output,
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
	void DrawFooterButtons();

	void LoadFromProject();
	bool ApplyToProject(std::string* outError);
	void MarkDirty();

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
	bool m_loadedFromProject = false;
	bool m_dirty = false;
	std::string m_errorMessage;
};

#endif
