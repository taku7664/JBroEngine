#pragma once

#include "Engine/Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/Core/Asset/AssetTypes.h"
#include "Utillity/SafePtr.h"

class IAssetManager;
class CScriptModuleLoader;
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
	std::uint64_t GetAssetDatabaseRevision() const;
	SafePtr<IAssetManager> GetAssetManager() const;
	void BuildAssetRegistrySnapshot(AssetRegistrySnapshot& outSnapshot) const;

	// 프로젝트 해상도
	std::uint32_t GetResolutionWidth()  const;
	std::uint32_t GetResolutionHeight() const;
	void SetResolution(std::uint32_t width, std::uint32_t height);

	// 씬 뷰 에디터 카메라 (저장/복원용)
	float GetSceneViewCamX()    const;
	float GetSceneViewCamY()    const;
	float GetSceneViewCamSize() const;
	void  SetSceneViewCamera(float x, float y, float size);

	// 스크립트 DLL 경로
	const std::string& GetScriptDllPath() const;
	void               SetScriptDllPath(const std::string& path);

	// 마지막으로 열었던 씬 경로 (Assets 폴더 기준 상대경로)
	const std::string& GetLastOpenedScenePath() const;
	void               SetLastOpenedScenePath(const std::string& relativePath);

	// 스크립트 DLL 로드/언로드
	bool LoadScriptModule();
	void UnloadScriptModule();
	bool IsScriptModuleLoaded() const;

	// .Jproject 파일로 현재 설정을 저장합니다.
	bool SaveProject() const;

private:
	void ProcessAssetEvents(const std::vector<FileWatchEvent>& events);
	void ProcessAssetEvent(const FileWatchEvent& event);
	bool ImportOrReloadAsset(const File::Path& absolutePath);
	bool IsImportableAssetPath(const File::Path& absolutePath) const;
	bool MakeAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const;
	EAssetType DetectAssetType(const File::Path& relativePath) const;
	bool TrySyncRenamedAssetMeta(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events);
	bool TryGetMetaPathForAsset(const File::Path& assetPath, File::Path& outMetaPath) const;
	bool MoveMetaForRenamedAsset(const File::Path& oldAssetPath, const File::Path& newAssetPath) const;
	void MarkAssetDatabaseChanged();

private:
	SafePtr<IAssetManager>       m_assetManager;
	OwnerPtr<IFileWatcher>       m_assetWatcher;
	OwnerPtr<CScriptModuleLoader> m_scriptLoader;
	ProjectInfo                  m_info;
	// EngineContext 포인터는 스크립트 모듈 로드 시 GameModuleContext 구성에 사용합니다.
	const EngineContext*         m_engineContext = nullptr;
	std::uint64_t                m_assetDatabaseRevision = 0;
	bool                         m_isInitialized = false;
	bool                         m_isProjectLoaded = false;
};

