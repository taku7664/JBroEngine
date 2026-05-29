#pragma once

#include "Engine/Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Engine/Editor/LiveCompile/LiveCompileTypes.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/Core/Asset/AssetTypes.h"
#include "Utillity/SafePtr.h"

class IAssetManager;
class CScriptModuleLoader;
class CLiveCompileManager;
struct EngineCore;

class CProjectManager final : public EnableSafeFromThis<CProjectManager>
{
public:
	bool Initialize(const EngineCore& context);
	void Finalize();

	// 새 프로젝트 생성: {parentFolder}/{projectName}/ 디렉토리를 만들고
	// .Jproject + Contents/Assets + Contents/Scripts 구조를 초기화한 뒤 LoadProject 까지 수행합니다.
	bool CreateProject(const File::Path& parentFolder, const std::string& projectName);

	bool LoadProject(const ProjectLoadDesc& desc);
	void CloseProject();
	void Tick();

	bool IsProjectLoaded() const;
	const File::Path& GetOriginPath() const;
	const File::Path& GetProjectFilePath() const;
	const File::Path& GetRootPath() const;
	const File::Path& GetContentPath() const;
	const File::Path& GetContentFolder() const;
	const File::Path& GetAssetPath() const;
	const File::Path& GetScriptPath() const;
	std::uint64_t GetAssetDatabaseRevision() const;
	SafePtr<IAssetManager> GetAssetManager() const;
	void BuildAssetRegistrySnapshot(AssetRegistrySnapshot& outSnapshot) const;

	// 프로젝트 해상도
	std::uint32_t GetResolutionWidth()  const;
	std::uint32_t GetResolutionHeight() const;
	void SetResolution(std::uint32_t width, std::uint32_t height);

	// 좌표계 단위 (PPU: Pixels Per Unit)
	float GetPixelsPerUnit() const;
	void  SetPixelsPerUnit(float ppu);

	// 씬 뷰 에디터 카메라 (저장/복원용)
	float GetSceneViewCamX()    const;
	float GetSceneViewCamY()    const;
	float GetSceneViewCamSize() const;
	void  SetSceneViewCamera(float x, float y, float size);

	// 스크립트 DLL 경로
	const std::string& GetScriptDllPath() const;
	void               SetScriptDllPath(const std::string& path);

	const std::string& GetScriptSourceDirectory() const;
	void SetScriptSourceDirectory(const std::string& path);
	const std::string& GetScriptBuildCommand() const;
	void SetScriptBuildCommand(const std::string& command);
	const std::string& GetScriptOutputLibraryPath() const;
	void SetScriptOutputLibraryPath(const std::string& path);
	const std::string& GetScriptIntermediateDirectory() const;
	void SetScriptIntermediateDirectory(const std::string& path);
	EScriptBuildConfiguration GetScriptBuildConfiguration() const;
	void SetScriptBuildConfiguration(EScriptBuildConfiguration configuration);
	bool IsScriptAutoRebuildEnabled() const;
	void SetScriptAutoRebuildEnabled(bool enabled);
	bool IsLiveCompileEnabled() const;
	void SetLiveCompileEnabled(bool enabled);

	// 마지막으로 열었던 씬 경로 (Assets 폴더 기준 상대경로)
	const std::string& GetLastOpenedScenePath() const;
	void               SetLastOpenedScenePath(const std::string& relativePath);

	// 프로젝트별 에디터 상태
	const std::string& GetEditorLocaleCode() const;
	void               SetEditorLocaleCode(const std::string& localeCode);
	const std::string& GetImGuiIniSettings() const;
	void               SetImGuiIniSettings(const std::string& iniSettings);

	// 스크립트 DLL 로드/언로드
	bool LoadScriptModule();
	void UnloadScriptModule();
	bool IsScriptModuleLoaded() const;
	bool StartLiveCompile();
	void StopLiveCompile();
	bool RebuildScriptModule();
	bool IsLiveCompileLoaded() const;
	ELiveCompileState GetLiveCompileState() const;

	// 마지막 LiveCompile 실패의 풀 메시지(컴파일러 출력 포함).  소비형 — 한 번
	// 가져가면 자동으로 비워진다.  AssetBrowser 등 UI 가 매 프레임 폴링하여
	// 새 실패가 생기면 즉시 팝업으로 표시할 때 사용.
	std::string ConsumeLastLiveCompileFailure();

	// .Jproject 파일로 현재 설정을 저장합니다.
	bool SaveProject() const;

	// ── Visual Studio 연동 ────────────────────────────────────────────────────
	// 스크립트 소스 디렉토리 기준으로 .sln / .vcxproj 를 찾는다.
	// 발견되지 않으면 NULL_PATH 반환. 캐시 안 함 — 디스크 조회 비용은 작음.
	File::Path FindScriptSolutionPath() const;
	File::Path FindScriptVcxprojPath() const;

	// 스크립트 IDE(Windows: Visual Studio) 에서 프로젝트만 연다.
	// filePath 는 이전 API 호환용이며 현재는 소스 파일을 직접 열지 않는다.
	// 같은 프로젝트에 대해서는 중복 실행을 막는다.
	void OpenScriptInIde(const File::Path& filePath) const;

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
	GameModuleContext BuildGameModuleContext() const;
	LiveCompileDesc BuildLiveCompileDesc() const;

private:
	SafePtr<IAssetManager>       m_assetManager;
	OwnerPtr<IFileWatcher>       m_assetWatcher;
	OwnerPtr<CScriptModuleLoader> m_scriptLoader;
	OwnerPtr<CLiveCompileManager> m_liveCompileManager;
	ProjectInfo                  m_info;
	// EngineCore 포인터는 스크립트 모듈 로드 시 GameModuleContext 구성에 사용합니다.
	const EngineCore*            m_engineCore = nullptr;
	std::uint64_t                m_assetDatabaseRevision = 0;
	mutable File::Path           m_lastOpenedScriptIdePath;
	// 마지막 LiveCompile 실패 메시지 (소비형 — ConsumeLastLiveCompileFailure 로 회수).
	std::string                  m_lastLiveCompileFailure;
	bool                         m_isInitialized = false;
	bool                         m_isProjectLoaded = false;
};
