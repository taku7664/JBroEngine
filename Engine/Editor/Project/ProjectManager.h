#pragma once

#include "Engine/Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Utillity/SafePtr.h"

class IAssetManager;
struct EngineContext;

class CProjectManager final : public EnableSafeFromThis<CProjectManager>
{
public:
	bool Initialize(const EngineContext& context);
	void Finalize();

	bool LoadProject(const ProjectLoadDesc& desc);
	void CloseProject();
	void Tick();

	bool IsProjectLoaded() const;
	const File::Path& GetOriginPath() const;
	const File::Path& GetProjectFilePath() const;
	const File::Path& GetRootPath() const;
	const File::Path& GetAssetPath() const;

private:
	void ProcessAssetEvent(const FileWatchEvent& event);
	void ImportOrReloadAsset(const File::Path& absolutePath);
	bool IsImportableAssetPath(const File::Path& absolutePath) const;
	bool MakeAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const;
	EAssetType DetectAssetType(const File::Path& relativePath) const;

private:
	SafePtr<IAssetManager> m_assetManager;
	OwnerPtr<IFileWatcher> m_assetWatcher;
	ProjectInfo m_info;
	bool m_isInitialized = false;
	bool m_isProjectLoaded = false;
};

