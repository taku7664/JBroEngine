#pragma once

#include "Engine/Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Engine/Editor/LiveCompile/LiveCompileTypes.h"
#include "Engine/Editor/Project/ProjectTypes.h"
#include "Engine/Editor/Project/AssetDatabase.h"
#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Task/TaskTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

class IAssetManager;
class CScriptModuleLoader;
class CLiveCompileManager;
class CTaskGroup;

// 자산 정합성(reconcile) 패스 결과 요약 — 프로젝트 로드 시 무엇을 치유했는지.
struct AssetReconcileReport
{
	int Registered        = 0;   // 레지스트리에 등록된 자산 수
	int MetaGenerated     = 0;   // .Jmeta 가 없어 새로 생성(새 GUID)
	int GuidRecovered     = 0;   // 메타 분실/손상을 복구 캐시로 같은 GUID 복구(제자리)
	int Relinked          = 0;   // 이동된 자산을 해시로 추적해 같은 GUID 재링크
	int DuplicateResolved = 0;   // 중복 GUID 를 결정적으로 재발급
	int OrphanQuarantined = 0;   // raw 없는 고아 .Jmeta 격리
	int Failed            = 0;   // 등록 실패
};

class CProjectManager final : public EnableSafeFromThis<CProjectManager>
{
public:
	bool Initialize();
	void Finalize();

	// 새 프로젝트 생성: {parentFolder}/{projectName}/ 디렉토리를 만들고
	// .Jproject + Contents/Assets + Contents/Scripts 구조를 초기화한 뒤 LoadProject 까지 수행합니다.
	bool CreateProject(const File::Path& parentFolder, const std::string& projectName);

	bool LoadProject(const ProjectLoadDesc& desc);
	void CloseProject();
	void Tick();

	bool IsProjectLoaded() const;

	// ── 비동기 자산 로드 진행률 ─────────────────────────────────────────
	// LoadProject 가 디스크의 모든 자산을 CTaskManager 로 병렬 임포트하는 동안
	// 아래 메서드들로 UI 가 프로그레스를 표시할 수 있다.  완료되면 자동으로
	// nullptr 가 된다.
	bool          HasLoadingTasks() const;
	float         GetLoadProgress01() const;
	std::uint32_t GetLoadCompletedCount() const;
	std::uint32_t GetLoadTotalCount() const;
	// 로드 중인 각 태스크의 (이름/설명/완료여부) 스냅샷 — 진행률 팝업의 작업 목록 표시용.
	std::vector<TaskProgressInfo> GetLoadTaskSnapshot() const;
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

	// 절대경로가 프로젝트 자산 루트 아래면 상대경로로 변환해 true 반환.
	// AssetBrowser 등 외부 코드가 ProjectManager 가 등록한 자산을 공유하기 위해 사용.
	// 프로젝트 밖이면 false (호출자가 절대경로 fallback 등록을 결정).
	bool TryMakeProjectAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const;

	// 마지막 프로젝트 로드 시 수행한 자산 정합성 패스 결과(에디터 리포트 패널용).
	const AssetReconcileReport& GetLastReconcileReport() const;

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

	// 게임 빌드본 생성 설정
	const ProjectBuildSettings& GetBuildSettings() const;
	void SetBuildSettings(const ProjectBuildSettings& settings);

	// 프로젝트별 에디터 상태
	const std::string& GetEditorLocaleCode() const;
	void               SetEditorLocaleCode(const std::string& localeCode);
	const std::string& GetImGuiIniSettings() const;
	void               SetImGuiIniSettings(const std::string& iniSettings);

	// 자산 워처 무시 패턴 — glob (* / ?). 외부 도구 임시 파일(*.tmp, ~$*, *.swp 등) 을
	// 자산 import 시도에서 거른다. 사용자가 ProjectSettings 의 "Asset Watcher" 카테고리에서 편집.
	const std::vector<std::string>& GetAssetWatchIgnorePatterns() const;
	void                            SetAssetWatchIgnorePatterns(std::vector<std::string> patterns);
	// 경로(절대 또는 상대) 가 현재 무시 패턴 중 하나에라도 매칭되는지.
	bool IsAssetPathIgnored(const File::Path& absoluteOrRelativePath) const;

	// 스크립트 DLL 로드/언로드
	bool LoadScriptModule();
	void UnloadScriptModule();
	bool IsScriptModuleLoaded() const;
	bool StartLiveCompile();
	void StopLiveCompile();
	bool RebuildScriptModule();
	// 스크립트 파일 추가/삭제 후 GameScript 프로젝트(.vcxproj·생성 파일)를 재생성한다.
	// vcxproj 가 소스 목록을 명시적으로 들고 있어, 파일 변경 후 빌드 정합성을 위해 필요.
	bool RegenerateScriptProject() const;
	bool IsLiveCompileLoaded() const;
	ELiveCompileState GetLiveCompileState() const;

	// 마지막 LiveCompile 실패의 풀 메시지(컴파일러 출력 포함).  소비형 — 한 번
	// 가져가면 자동으로 비워진다.  AssetBrowser 등 UI 가 매 프레임 폴링하여
	// 새 실패가 생기면 즉시 팝업으로 표시할 때 사용.
	std::string ConsumeLastLiveCompileFailure();

	// .Jproject 파일로 현재 설정을 저장합니다.
	bool SaveProject(std::string* outError = nullptr) const;

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
	// loadData=true: 메타 등록 + 데이터 로드(ReloadAsset). false: 메타 등록만(데이터 미로드).
	bool ImportOrReloadAsset(const File::Path& absolutePath, bool loadData = true);
	// 마지막 씬(상대경로)이 참조하는 에셋 GUID 를 전이적으로 수집한다(프리팹 참조 포함).
	std::vector<AssetGuid> CollectSceneLoadAssets(const std::string& sceneRelativePath) const;
	bool IsImportableAssetPath(const File::Path& absolutePath) const;
	bool MakeAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const;
	EAssetType DetectAssetType(const File::Path& relativePath) const;
	bool TrySyncRenamedAssetMeta(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events);
	// 이동/이름변경(Deleted(old)+Created(new) 쌍)을 감지해 .Jmeta 를 동반 이동하고
	// 레지스트리 경로만 in-place 갱신한다(언로드 X → 로드된 에셋/라이브 참조 보존).
	// 처리했으면 true 와 함께 outOldAssetPath 에 원본 경로를 채운다.
	bool TryHandleAssetRename(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events, File::Path& outOldAssetPath);
	bool TryGetMetaPathForAsset(const File::Path& assetPath, File::Path& outMetaPath) const;
	bool MoveMetaForRenamedAsset(const File::Path& oldAssetPath, const File::Path& newAssetPath) const;
	void MarkAssetDatabaseChanged();

	// ── 자산 정합성(reconcile) + 복구 캐시 ──────────────────────────────────
	File::Path GetAssetDbPath() const;
	// 타입별 임포터 표시 이름(인스펙터 "임포터" 필드).
	static const char* ImporterNameForType(EAssetType type);
	// Assets 트리 1회 정합성 패스 — 메타 생성/GUID 복구/중복 해소/고아 격리 + 레지스트리 등록.
	AssetReconcileReport ReconcileAssets();
	// raw 없는 고아 .Jmeta 를 Intermediate/AssetQuarantine 로 이동(되돌릴 수 있게).
	bool QuarantineOrphanMeta(const File::Path& metaAbsolutePath, const std::string& assetRelativePath);
	// 메타가 없을 때 복구 캐시로 같은 GUID 의 사이드카 메타를 먼저 써 둔다(런타임 복구).
	void EnsureRecoveredSidecarMeta(const File::Path& absolutePath, const std::string& relativePath, EAssetType type, const char* importerName, const File::Path& metaPath);
	// 복구 캐시 항목 갱신(경로/해시/메타 필드).
	void UpdateAssetDbEntry(const AssetMetaData& metaData, const File::Path& absolutePath, const std::string& relativePath);
	GameModuleContext BuildGameModuleContext() const;
	LiveCompileDesc BuildLiveCompileDesc() const;

private:
	SafePtr<IAssetManager>       m_assetManager;
	OwnerPtr<IFileWatcher>       m_assetWatcher;
	OwnerPtr<CScriptModuleLoader> m_scriptLoader;
	OwnerPtr<CLiveCompileManager> m_liveCompileManager;
	ProjectInfo                  m_info;
	std::uint64_t                m_assetDatabaseRevision = 0;
	// 자산 복구 캐시(비권위적). reconcile/런타임 임포트가 갱신, CloseProject 에서 저장.
	CAssetDatabase               m_assetDb;
	bool                         m_assetDbDirty = false;
	AssetReconcileReport         m_lastReconcileReport;
	// 파일 워처 폴링 스로틀(전체 트리 walk 비용 완화).
	std::chrono::steady_clock::time_point m_lastWatchPollTime{};
	mutable File::Path           m_lastOpenedScriptIdePath;
	// 마지막 LiveCompile 실패 메시지 (소비형 — ConsumeLastLiveCompileFailure 로 회수).
	std::string                  m_lastLiveCompileFailure;
	// 비동기 자산 로드 — LoadProject 가 set, 모든 작업 완료 시 main thread 콜백에서 reset.
	SafePtr<CTaskGroup>          m_loadTaskGroup;
	// LoadProject 완료 시점에 실행되어야 하는 후처리 (live compile init 등).
	// CTaskGroup 의 AllCompletedCallback 안에서 호출.
	std::function<void()>        m_postLoadAction;
	bool                         m_isInitialized = false;
	bool                         m_isProjectLoaded = false;
};
