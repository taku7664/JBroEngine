#include "pch.h"
#include "ProjectManager.h"

#include "Core/Asset/AssetPath.h"
#include "Core/EngineCore.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Game/GameModuleTypes.h"
#include "File/FileUtillities.h"
#include "Editor/Project/GameScriptProjectGenerator.h"
#include "Editor/LiveCompile/LiveCompileManager.h"
#include "Editor/ScriptModule/ScriptModuleLoader.h"
#include "GameFramework/Scene/SceneManager.h"
#include "yaml-cpp/yaml.h"

#include <chrono>
#include <cstdlib>

namespace
{
	constexpr const char* PROJECT_EXTENSION             = ".Jproject";
	constexpr const char* PROJECT_KEY_VERSION           = "Version";
	constexpr const char* PROJECT_KEY_ROOT_PATH         = "RootPath";
	constexpr const char* PROJECT_KEY_RES_WIDTH         = "ResolutionWidth";
	constexpr const char* PROJECT_KEY_RES_HEIGHT        = "ResolutionHeight";
	constexpr const char* PROJECT_KEY_SCENE_CAM_X       = "SceneViewCamX";
	constexpr const char* PROJECT_KEY_SCENE_CAM_Y       = "SceneViewCamY";
	constexpr const char* PROJECT_KEY_SCENE_CAM_SIZE    = "SceneViewCamSize";
	constexpr const char* PROJECT_KEY_SCRIPT_DLL_PATH   = "ScriptDllPath";
	constexpr const char* PROJECT_KEY_SCRIPT_SOURCE_DIR  = "ScriptSourceDirectory";
	constexpr const char* PROJECT_KEY_SCRIPT_BUILD_CMD   = "ScriptBuildCommand";
	constexpr const char* PROJECT_KEY_SCRIPT_OUTPUT_DLL  = "ScriptOutputLibraryPath";
	constexpr const char* PROJECT_KEY_SCRIPT_INTERMEDIATE = "ScriptIntermediateDirectory";
	constexpr const char* PROJECT_KEY_SCRIPT_BUILD_CONFIG = "ScriptBuildConfiguration";
	constexpr const char* PROJECT_KEY_SCRIPT_AUTO_REBUILD_ENABLED = "ScriptAutoRebuildEnabled";
	constexpr const char* PROJECT_KEY_LEGACY_LIVE_COMPILE_ENABLED = "LiveCompileEnabled";
	constexpr const char* PROJECT_KEY_LAST_SCENE_PATH   = "LastOpenedScenePath";
	constexpr const char* PROJECT_KEY_PIXELS_PER_UNIT   = "PixelsPerUnit";
	constexpr const char* PROJECT_KEY_EDITOR_LOCALE     = "EditorLocale";
	constexpr const char* PROJECT_KEY_IMGUI_INI         = "ImGuiIniSettings";
	constexpr const char* CONTENTS_DIRECTORY_NAME       = "Contents";
	constexpr const char* ASSETS_DIRECTORY_NAME         = "Assets";
	constexpr const char* SCRIPTS_DIRECTORY_NAME        = "Scripts";

	std::string QuoteCommandPath(const std::filesystem::path& path)
	{
		std::string value = path.string();
		std::replace(value.begin(), value.end(), '\\', '/');
		return "\"" + value + "\"";
	}

	bool FindExistingPath(const std::vector<std::filesystem::path>& candidates, std::filesystem::path& outPath)
	{
		std::error_code errorCode;
		for (const std::filesystem::path& candidate : candidates)
		{
			if (std::filesystem::exists(candidate, errorCode))
			{
				outPath = candidate;
				return true;
			}
			errorCode.clear();
		}
		return false;
	}

	std::filesystem::path GetEnvironmentPath(const char* name)
	{
		char* value = nullptr;
		std::size_t length = 0;
		if (0 != _dupenv_s(&value, &length, name) || nullptr == value)
		{
			return {};
		}

		std::filesystem::path result(value);
		std::free(value);
		return result;
	}

	std::filesystem::path FindMSBuildPath()
	{
		std::vector<std::filesystem::path> candidates;

		const std::filesystem::path vsInstallDir = GetEnvironmentPath("VSINSTALLDIR");
		if (false == vsInstallDir.empty())
		{
			candidates.push_back(vsInstallDir / "MSBuild" / "Current" / "Bin" / "MSBuild.exe");
		}

		const std::filesystem::path programFilesX86 = GetEnvironmentPath("ProgramFiles(x86)");
		const std::filesystem::path programFiles = GetEnvironmentPath("ProgramFiles");
		const std::vector<std::filesystem::path> roots =
		{
			programFilesX86,
			programFiles
		};
		const char* editions[] =
		{
			"Community",
			"Professional",
			"Enterprise",
			"BuildTools"
		};

		for (const std::filesystem::path& root : roots)
		{
			if (root.empty())
			{
				continue;
			}
			for (const char* edition : editions)
			{
				candidates.push_back(root / "Microsoft Visual Studio" / "2022" / edition / "MSBuild" / "Current" / "Bin" / "MSBuild.exe");
			}
		}

		std::filesystem::path msbuildPath;
		if (FindExistingPath(candidates, msbuildPath))
		{
			return msbuildPath;
		}

		return "MSBuild.exe";
	}

	std::filesystem::path ResolveLiveCompileBasePath(const ProjectInfo& info)
	{
		const std::filesystem::path rootSourceProject = info.RootPath / info.ScriptSourceDirectory / "GameScript.vcxproj";
		const std::filesystem::path originSourceProject = info.OriginPath / info.ScriptSourceDirectory / "GameScript.vcxproj";

		std::error_code errorCode;
		if (std::filesystem::exists(rootSourceProject, errorCode))
		{
			return info.RootPath;
		}
		errorCode.clear();
		if (std::filesystem::exists(originSourceProject, errorCode))
		{
			return info.OriginPath;
		}

		return false == info.RootPath.empty() ? info.RootPath : info.OriginPath;
	}

	std::filesystem::path ResolveLiveCompilePath(const std::filesystem::path& basePath, const std::string& path)
	{
		std::filesystem::path resolved(path);
		if (resolved.is_absolute())
		{
			return resolved;
		}
		return basePath / resolved;
	}

	std::string BuildDefaultLiveCompileCommand(const ProjectInfo& info, const std::filesystem::path& basePath)
	{
		const std::filesystem::path projectPath = ResolveLiveCompilePath(basePath, info.ScriptSourceDirectory) / "GameScript.vcxproj";
		const std::filesystem::path msbuildPath = FindMSBuildPath();
		const std::filesystem::path solutionDir = basePath;
		const char* configuration = EScriptBuildConfiguration::Release == info.ScriptBuildConfiguration ? "Release" : "Debug";

		std::string solutionDirValue = solutionDir.string();
		if (false == solutionDirValue.empty() && solutionDirValue.back() != '\\' && solutionDirValue.back() != '/')
		{
			solutionDirValue += "\\";
		}

		// /m (멀티 프로세스 빌드) 제거 — Player.cpp 등 소량의 cpp 만 빌드하므로 의미가 없고,
		// 자식 MSBuild 노드가 좀비로 남아 mspdbsrv 잠금 경합을 일으키는 원인이 된다.
		// CL_MPCount=1 로 cl.exe 의 multi-process compilation 도 강제 비활성화.
		// /nr:false : node reuse 비활성 — 빌드 종료 시 MSBuild 노드 즉시 종료.
		return QuoteCommandPath(msbuildPath)
			+ " " + QuoteCommandPath(projectPath)
			+ " /p:Configuration=" + configuration
			+ " /p:Platform=x64"
			+ " /p:SolutionDir=" + QuoteCommandPath(solutionDirValue)
			+ " /p:JBroLiveCompileStamp=" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
			+ " /p:CL_MPCount=1"
			+ " /p:UseMultiToolTask=false"
			+ " /v:minimal"
			+ " /nr:false";
	}

	EScriptBuildConfiguration ParseScriptBuildConfiguration(const std::string& value)
	{
		if (value == "Release")
		{
			return EScriptBuildConfiguration::Release;
		}
		return EScriptBuildConfiguration::Debug;
	}

	const char* ToString(EScriptBuildConfiguration configuration)
	{
		return EScriptBuildConfiguration::Release == configuration ? "Release" : "Debug";
	}

	void LogLiveCompileFailure(const char* title, const LiveCompileResult& result)
	{
		CSystemLog::Error(title);

		std::istringstream stream(result.Message);
		std::string line;
		while (std::getline(stream, line))
		{
			if (line.empty())
			{
				continue;
			}
			CSystemLog::Error(std::string("  ") + line);
		}
	}
}

bool CProjectManager::Initialize(const EngineCore& context)
{
	m_assetManager  = context.AssetManager;
	m_assetWatcher  = MakeOwnerPtr<CWindowsFileWatcher>();
	m_scriptLoader  = MakeOwnerPtr<CScriptModuleLoader>();
	m_liveCompileManager = MakeOwnerPtr<CLiveCompileManager>();
	m_engineCore = &context;
	m_info.OriginPath = File::Path(std::filesystem::current_path());
	m_isInitialized = m_assetManager.IsValid() && static_cast<bool>(m_assetWatcher);
	return m_isInitialized;
}

void CProjectManager::Finalize()
{
	CloseProject();
	m_scriptLoader.Reset();
	m_liveCompileManager.Reset();
	m_assetWatcher.Reset();
	m_assetManager  = nullptr;
	m_engineCore = nullptr;
	m_isInitialized = false;
}

bool CProjectManager::CreateProject(const File::Path& parentFolder, const std::string& projectName)
{
	if (false == m_isInitialized || parentFolder.empty() || projectName.empty())
	{
		return false;
	}

	// 프로젝트 루트: {parentFolder}/{projectName}/
	const std::filesystem::path projectRoot = parentFolder / projectName;
	const std::filesystem::path contentsDir = projectRoot / CONTENTS_DIRECTORY_NAME;
	const std::filesystem::path assetsDir   = contentsDir / ASSETS_DIRECTORY_NAME;
	const std::filesystem::path scriptsDir  = contentsDir / SCRIPTS_DIRECTORY_NAME;

	std::error_code ec;
	std::filesystem::create_directories(assetsDir, ec);
	if (ec)
	{
		return false;
	}
	std::filesystem::create_directories(scriptsDir, ec);
	if (ec)
	{
		return false;
	}

	// .Jproject 파일 기본 내용: 루트 경로 = ".", 나머지 기본값
	const std::filesystem::path projectFile = projectRoot / (projectName + PROJECT_EXTENSION);
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << PROJECT_KEY_VERSION                    << YAML::Value << 1;
		out << YAML::Key << PROJECT_KEY_ROOT_PATH                  << YAML::Value << ".";
		out << YAML::Key << PROJECT_KEY_RES_WIDTH                  << YAML::Value << 1920;
		out << YAML::Key << PROJECT_KEY_RES_HEIGHT                 << YAML::Value << 1080;
		// 새 프로젝트는 자동 리빌드 기본 ON. 키를 명시 저장해 로드 시 default 의존 X.
		out << YAML::Key << PROJECT_KEY_SCRIPT_AUTO_REBUILD_ENABLED << YAML::Value << true;
		out << YAML::EndMap;

		std::ofstream file(projectFile, std::ios::out | std::ios::trunc);
		if (false == file.is_open())
		{
			return false;
		}
		file << out.c_str();
	}

	// LoadProject 로 이어서 초기화 (Contents 폴더 + 스크립트 프로젝트 파일 생성 포함)
	ProjectLoadDesc loadDesc;
	loadDesc.ProjectFilePath = File::Path(projectFile);
	return LoadProject(loadDesc);
}

bool CProjectManager::LoadProject(const ProjectLoadDesc& desc)
{
	if (false == m_isInitialized || false == m_assetManager.IsValid() || desc.ProjectFilePath.empty())
	{
		return false;
	}

	const std::filesystem::path projectPath = std::filesystem::absolute(desc.ProjectFilePath);
	if (projectPath.extension() != PROJECT_EXTENSION)
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(projectPath, errorCode) || false == std::filesystem::is_regular_file(projectPath, errorCode))
	{
		return false;
	}

	YAML::Node root;
	try
	{
		root = YAML::LoadFile(projectPath.string());
	}
	catch (const YAML::Exception&)
	{
		return false;
	}

	std::uint32_t version = 1;
	if (root[PROJECT_KEY_VERSION])
	{
		version = root[PROJECT_KEY_VERSION].as<std::uint32_t>(1);
	}

	std::uint32_t resolutionWidth  = 1920;
	std::uint32_t resolutionHeight = 1080;
	if (root[PROJECT_KEY_RES_WIDTH])
	{
		resolutionWidth = root[PROJECT_KEY_RES_WIDTH].as<std::uint32_t>(1920);
	}
	if (root[PROJECT_KEY_RES_HEIGHT])
	{
		resolutionHeight = root[PROJECT_KEY_RES_HEIGHT].as<std::uint32_t>(1080);
	}

	float sceneViewCamX    = 0.0f;
	float sceneViewCamY    = 0.0f;
	float sceneViewCamSize = 5.0f;
	if (root[PROJECT_KEY_SCENE_CAM_X])
	{
		sceneViewCamX = root[PROJECT_KEY_SCENE_CAM_X].as<float>(0.0f);
	}
	if (root[PROJECT_KEY_SCENE_CAM_Y])
	{
		sceneViewCamY = root[PROJECT_KEY_SCENE_CAM_Y].as<float>(0.0f);
	}
	if (root[PROJECT_KEY_SCENE_CAM_SIZE])
	{
		sceneViewCamSize = root[PROJECT_KEY_SCENE_CAM_SIZE].as<float>(5.0f);
		if (sceneViewCamSize <= 0.0f) sceneViewCamSize = 5.0f;
	}

	std::string scriptDllPath;
	if (root[PROJECT_KEY_SCRIPT_DLL_PATH])
	{
		scriptDllPath = root[PROJECT_KEY_SCRIPT_DLL_PATH].as<std::string>("");
	}

	std::string scriptSourceDirectory = CONTENTS_DIRECTORY_NAME;

	std::string scriptBuildCommand;
	if (root[PROJECT_KEY_SCRIPT_BUILD_CMD])
	{
		scriptBuildCommand = root[PROJECT_KEY_SCRIPT_BUILD_CMD].as<std::string>("");
	}

	std::string scriptOutputLibraryPath = "x64/Debug/GameScript.dll";
	if (root[PROJECT_KEY_SCRIPT_OUTPUT_DLL])
	{
		scriptOutputLibraryPath = root[PROJECT_KEY_SCRIPT_OUTPUT_DLL].as<std::string>("x64/Debug/GameScript.dll");
	}

	std::string scriptIntermediateDirectory = "Build/Intermediate/LiveCompile";
	if (root[PROJECT_KEY_SCRIPT_INTERMEDIATE])
	{
		scriptIntermediateDirectory = root[PROJECT_KEY_SCRIPT_INTERMEDIATE].as<std::string>("Build/Intermediate/LiveCompile");
	}

	EScriptBuildConfiguration scriptBuildConfiguration = EScriptBuildConfiguration::Debug;
	if (root[PROJECT_KEY_SCRIPT_BUILD_CONFIG])
	{
		scriptBuildConfiguration = ParseScriptBuildConfiguration(root[PROJECT_KEY_SCRIPT_BUILD_CONFIG].as<std::string>("Debug"));
	}

	bool scriptAutoRebuildEnabled = true;   // 기본값 — 프로젝트 파일에 키가 없을 때 적용
	if (root[PROJECT_KEY_SCRIPT_AUTO_REBUILD_ENABLED])
	{
		scriptAutoRebuildEnabled = root[PROJECT_KEY_SCRIPT_AUTO_REBUILD_ENABLED].as<bool>(true);
	}
	else if (root[PROJECT_KEY_LEGACY_LIVE_COMPILE_ENABLED])
	{
		scriptAutoRebuildEnabled = root[PROJECT_KEY_LEGACY_LIVE_COMPILE_ENABLED].as<bool>(true);
	}

	std::string lastOpenedScenePath;
	if (root[PROJECT_KEY_LAST_SCENE_PATH])
	{
		lastOpenedScenePath = root[PROJECT_KEY_LAST_SCENE_PATH].as<std::string>("");
	}

	std::string editorLocaleCode = (m_engineCore && m_engineCore->Localization.IsValid())
		? m_engineCore->Localization->GetDefaultLocale()
		: "ko-KR";
	if (root[PROJECT_KEY_EDITOR_LOCALE])
	{
		editorLocaleCode = root[PROJECT_KEY_EDITOR_LOCALE].as<std::string>(editorLocaleCode);
	}

	std::string imguiIniSettings;
	if (root[PROJECT_KEY_IMGUI_INI])
	{
		imguiIniSettings = root[PROJECT_KEY_IMGUI_INI].as<std::string>("");
	}

	float pixelsPerUnit = 100.0f;
	if (root[PROJECT_KEY_PIXELS_PER_UNIT])
	{
		pixelsPerUnit = root[PROJECT_KEY_PIXELS_PER_UNIT].as<float>(100.0f);
		if (pixelsPerUnit < 1.0f) pixelsPerUnit = 1.0f;
	}

	std::filesystem::path rootRelativePath = ".";
	if (root[PROJECT_KEY_ROOT_PATH])
	{
		rootRelativePath = root[PROJECT_KEY_ROOT_PATH].as<std::string>();
	}
	if (rootRelativePath.is_absolute())
	{
		return false;
	}

	std::filesystem::path projectRootPath = std::filesystem::weakly_canonical(projectPath.parent_path() / rootRelativePath, errorCode);
	if (errorCode)
	{
		errorCode.clear();
		projectRootPath = std::filesystem::absolute(projectPath.parent_path() / rootRelativePath);
	}

	std::filesystem::path contentPath = projectRootPath / CONTENTS_DIRECTORY_NAME;
	std::filesystem::path assetPath   = contentPath / ASSETS_DIRECTORY_NAME;
	std::filesystem::path scriptPath  = contentPath / SCRIPTS_DIRECTORY_NAME;

	std::filesystem::create_directories(contentPath, errorCode);
	if (errorCode || false == std::filesystem::is_directory(contentPath, errorCode))
	{
		return false;
	}
	errorCode.clear();
	std::filesystem::create_directories(assetPath, errorCode);
	if (errorCode || false == std::filesystem::is_directory(assetPath, errorCode))
	{
		return false;
	}
	errorCode.clear();
	std::filesystem::create_directories(scriptPath, errorCode);
	if (errorCode || false == std::filesystem::is_directory(scriptPath, errorCode))
	{
		return false;
	}
	errorCode.clear();

	CloseProject();

	m_info.Version          = version;
	m_info.ProjectFilePath  = File::Path(projectPath);
	m_info.RootPath         = File::Path(projectRootPath);
	m_info.ContentPath      = File::Path(contentPath);
	m_info.AssetPath        = File::Path(assetPath);
	m_info.ScriptPath       = File::Path(scriptPath);
	m_info.ResolutionWidth  = (resolutionWidth  > 0) ? resolutionWidth  : 1920;
	m_info.ResolutionHeight = (resolutionHeight > 0) ? resolutionHeight : 1080;
	m_info.SceneViewCamX    = sceneViewCamX;
	m_info.SceneViewCamY    = sceneViewCamY;
	m_info.SceneViewCamSize = sceneViewCamSize;
	m_info.ScriptDllPath    = scriptDllPath;
	m_info.ScriptSourceDirectory = scriptSourceDirectory;
	m_info.ScriptBuildCommand = scriptBuildCommand;
	m_info.ScriptOutputLibraryPath = scriptOutputLibraryPath;
	m_info.ScriptIntermediateDirectory = scriptIntermediateDirectory;
	m_info.ScriptBuildConfiguration = scriptBuildConfiguration;
	m_info.ScriptAutoRebuildEnabled = scriptAutoRebuildEnabled;
	m_info.LastOpenedScenePath = lastOpenedScenePath;
	m_info.PixelsPerUnit       = pixelsPerUnit;
	m_info.EditorLocaleCode    = editorLocaleCode;
	m_info.ImGuiIniSettings    = imguiIniSettings;

	if (false == m_assetManager->SetAssetRootPath(m_info.AssetPath))
	{
		return false;
	}

	CGameScriptProjectGenerator gameScriptProjectGenerator;
	if (false == gameScriptProjectGenerator.EnsureProject(m_info))
	{
		return false;
	}

	FileWatcherDesc watcherDesc;
	watcherDesc.RootPath = m_info.AssetPath;
	watcherDesc.Recursive = true;
	m_assetWatcher->Watch(watcherDesc);

	for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(m_info.AssetPath, errorCode))
	{
		if (errorCode)
		{
			break;
		}
		if (entry.is_regular_file(errorCode))
		{
			ImportOrReloadAsset(File::Path(entry.path()));
		}
	}

	m_isProjectLoaded = true;
	MarkAssetDatabaseChanged();

	if (m_engineCore && m_engineCore->Localization.IsValid())
	{
		if (false == m_engineCore->Localization->SetCurrentLocale(m_info.EditorLocaleCode))
		{
			m_info.EditorLocaleCode = m_engineCore->Localization->GetCurrentLocale();
		}
	}
	if (false == m_info.ImGuiIniSettings.empty())
	{
		ImGui::LoadIniSettingsFromMemory(m_info.ImGuiIniSettings.c_str(), m_info.ImGuiIniSettings.size());
	}
	if (m_liveCompileManager)
	{
		if (m_liveCompileManager->Initialize(BuildLiveCompileDesc()))
		{
			const LiveCompileResult result = m_liveCompileManager->RebuildAndReload();
			if (result.Succeeded)
			{
				CSystemLog::Info("Script module loaded.");
			}
			else
			{
				LogLiveCompileFailure("Script module load failed.", result);
			}
		}
		else
		{
			CSystemLog::Error("Script live compile initialization failed.");
		}
	}
	return true;
}

void CProjectManager::CloseProject()
{
	// 스크립트 DLL 먼저 언로드 (씬보다 먼저)
	if (m_liveCompileManager)
	{
		m_liveCompileManager->Finalize();
	}
	if (m_scriptLoader)
	{
		m_scriptLoader->Unload();
	}

	if (m_assetWatcher)
	{
		m_assetWatcher->Stop();
	}

	// 프로젝트 자산만 정리. Persistent 로 표시된 자산(엔진/에디터 영구 리소스 등)은 보존.
	if (m_assetManager.IsValid())
	{
		m_assetManager->UnloadNonPersistentAssets();
	}

	m_isProjectLoaded = false;
	m_info.ProjectFilePath = File::NULL_PATH;
	m_info.RootPath        = File::NULL_PATH;
	m_info.ContentPath     = File::NULL_PATH;
	m_info.AssetPath       = File::NULL_PATH;
	m_info.ScriptPath      = File::NULL_PATH;
	m_info.ScriptDllPath   = {};
	m_info.ScriptSourceDirectory = CONTENTS_DIRECTORY_NAME;
	m_info.ScriptBuildCommand = {};
	m_info.ScriptOutputLibraryPath = "x64/Debug/GameScript.dll";
	m_info.ScriptIntermediateDirectory = "Build/Intermediate/LiveCompile";
	m_info.ScriptBuildConfiguration = EScriptBuildConfiguration::Debug;
	m_info.ScriptAutoRebuildEnabled = false;
	m_info.LastOpenedScenePath = {};
	m_info.ImGuiIniSettings    = {};
	m_lastOpenedScriptIdePath = File::NULL_PATH;
	MarkAssetDatabaseChanged();
}

void CProjectManager::Tick()
{
	if (false == m_isProjectLoaded || false == static_cast<bool>(m_assetWatcher))
	{
		return;
	}

	m_assetWatcher->Poll();
	std::vector<FileWatchEvent> events;
	if (m_assetWatcher->TakeEvents(events))
	{
		ProcessAssetEvents(events);
	}

	if (m_liveCompileManager)
	{
		// ApplicationFocusGained 는 "획득 한 프레임" 만 true 인 transient 플래그라
		// 자동 리빌드 게이트로 부적합 — 디바운스 0.5s 가 흐르기 전에 게이트가 닫혀
		// 빌드가 시작되지 못한다. 단순히 ScriptAutoRebuildEnabled 만 전달.
		m_liveCompileManager->Tick(m_info.ScriptAutoRebuildEnabled);
	}
}

bool CProjectManager::IsProjectLoaded() const
{
	return m_isProjectLoaded;
}

const File::Path& CProjectManager::GetOriginPath() const
{
	return m_info.OriginPath;
}

const File::Path& CProjectManager::GetProjectFilePath() const
{
	return m_info.ProjectFilePath;
}

const File::Path& CProjectManager::GetRootPath() const
{
	return m_info.RootPath;
}

const File::Path& CProjectManager::GetContentPath() const
{
	return m_info.ContentPath;
}

const File::Path& CProjectManager::GetContentFolder() const
{
	return m_info.ContentPath;
}

const File::Path& CProjectManager::GetAssetPath() const
{
	return m_info.AssetPath;
}

const File::Path& CProjectManager::GetScriptPath() const
{
	return m_info.ScriptPath;
}

std::uint64_t CProjectManager::GetAssetDatabaseRevision() const
{
	return m_assetDatabaseRevision;
}

SafePtr<IAssetManager> CProjectManager::GetAssetManager() const
{
	return m_assetManager;
}

void CProjectManager::BuildAssetRegistrySnapshot(AssetRegistrySnapshot& outSnapshot) const
{
	outSnapshot.Assets.clear();
	if (m_assetManager)
	{
		m_assetManager->GetRegistry().BuildSnapshot(outSnapshot);
	}
}

void CProjectManager::ProcessAssetEvents(const std::vector<FileWatchEvent>& events)
{
	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Created == event.Type)
		{
			TrySyncRenamedAssetMeta(event.Path, events);
		}
	}

	for (const FileWatchEvent& event : events)
	{
		ProcessAssetEvent(event);
	}
}

void CProjectManager::ProcessAssetEvent(const FileWatchEvent& event)
{
	if (false == m_assetManager.IsValid())
	{
		return;
	}

	if (EFileWatchEventType::Deleted == event.Type)
	{
		std::string relativePath;
		if (MakeAssetRelativePath(event.Path, relativePath))
		{
			File::Path registryPath(relativePath);
			if (CAssetPath::IsMetaPath(registryPath.generic_string().c_str()))
			{
				registryPath = File::Path(CAssetPath::StripMetaExtension(registryPath.generic_string().c_str()));
				const File::Path assetAbsolutePath = m_info.AssetPath / registryPath;
				std::error_code ec;
				if (std::filesystem::exists(assetAbsolutePath, ec))
				{
					if (ImportOrReloadAsset(assetAbsolutePath))
					{
						MarkAssetDatabaseChanged();
					}
					return;
				}
			}
			if (m_assetManager->UnregisterAssetByPath(registryPath, true))
			{
				MarkAssetDatabaseChanged();
			}
		}
		return;
	}

	if (ImportOrReloadAsset(event.Path))
	{
		MarkAssetDatabaseChanged();
	}
}

bool CProjectManager::ImportOrReloadAsset(const File::Path& absolutePath)
{
	if (false == IsImportableAssetPath(absolutePath))
	{
		return false;
	}

	std::string relativePath;
	if (false == MakeAssetRelativePath(absolutePath, relativePath))
	{
		return false;
	}

	const EAssetType type = DetectAssetType(relativePath);
	if (EAssetType::Unknown == type)
	{
		return false;
	}

	// 자산 타입별 임포터 이름. 인스펙터의 "임포터" 필드에 그대로 표시되므로
	// "Default" 같은 모호한 표기 대신 타입명을 사용.
	const char* importerName = "Default";
	switch (type)
	{
	case EAssetType::Sprite:   importerName = "Sprite";   break;
	case EAssetType::Material: importerName = "Material"; break;
	case EAssetType::Shader:   importerName = "Shader";   break;
	case EAssetType::Scene:    importerName = "Scene";    break;
	case EAssetType::Prefab:   importerName = "Prefab";   break;
	case EAssetType::Script:   importerName = "Script";   break;
	case EAssetType::Mesh:     importerName = "Mesh";     break;
	default:                   importerName = "Default";  break;
	}

	AssetImportDesc importDesc;
	importDesc.Type = type;
	importDesc.Path = File::Path(relativePath);
	importDesc.Importer = importerName;
	AssetMetaData metaData;
	if (m_assetManager->ImportAsset(importDesc, &metaData))
	{
		m_assetManager->ReloadAsset(metaData.Guid);
		return true;
	}
	return false;
}

bool CProjectManager::IsImportableAssetPath(const File::Path& absolutePath) const
{
	if (absolutePath.empty() || CAssetPath::IsMetaPath(absolutePath.generic_string().c_str()))
	{
		return false;
	}

	std::error_code errorCode;
	return std::filesystem::exists(absolutePath, errorCode) && std::filesystem::is_regular_file(absolutePath, errorCode);
}

bool CProjectManager::MakeAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const
{
	std::error_code errorCode;
	std::filesystem::path relativePath = std::filesystem::relative(absolutePath, m_info.AssetPath, errorCode);
	if (errorCode)
	{
		return false;
	}

	return CAssetPath::NormalizeRelativePath(relativePath.generic_string().c_str(), outRelativePath);
}

EAssetType CProjectManager::DetectAssetType(const File::Path& relativePath) const
{
	std::string extension = relativePath.extension().generic_string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});

	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga")
	{
		// 통합 이후 이미지 파일은 항상 Sprite (None/CellCount/CellSize 모드로 슬라이스 옵션).
		return EAssetType::Sprite;
	}
	if (extension == ".jscene")
	{
		return EAssetType::Scene;
	}
	if (extension == ".jprefab")
	{
		return EAssetType::Prefab;
	}
	if (extension == ".hlsl" || extension == ".wgsl" || extension == ".glsl")
	{
		return EAssetType::Shader;
	}
	if (extension == ".cpp" || extension == ".h" || extension == ".hpp")
	{
		return EAssetType::Script;
	}

	return EAssetType::Custom;
}

bool CProjectManager::TrySyncRenamedAssetMeta(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events)
{
	if (createdAssetPath.empty() || CAssetPath::IsMetaPath(createdAssetPath.generic_string().c_str()))
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(createdAssetPath, errorCode) || false == std::filesystem::is_regular_file(createdAssetPath, errorCode))
	{
		return false;
	}

	File::Path newMetaPath;
	if (false == TryGetMetaPathForAsset(createdAssetPath, newMetaPath) || std::filesystem::exists(newMetaPath, errorCode))
	{
		return false;
	}
	errorCode.clear();

	const std::string newExtension = createdAssetPath.extension().generic_string();
	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Deleted != event.Type || event.Path.empty() || CAssetPath::IsMetaPath(event.Path.generic_string().c_str()))
		{
			continue;
		}

		if (event.Path.extension().generic_string() != newExtension)
		{
			continue;
		}

		if (MoveMetaForRenamedAsset(event.Path, createdAssetPath))
		{
			return true;
		}
	}

	return false;
}

bool CProjectManager::TryGetMetaPathForAsset(const File::Path& assetPath, File::Path& outMetaPath) const
{
	std::string relativePath;
	if (false == MakeAssetRelativePath(assetPath, relativePath))
	{
		return false;
	}

	outMetaPath = m_info.AssetPath / File::Path(CAssetPath::MakeMetaPath(relativePath.c_str()));
	return true;
}

bool CProjectManager::MoveMetaForRenamedAsset(const File::Path& oldAssetPath, const File::Path& newAssetPath) const
{
	File::Path oldMetaPath;
	File::Path newMetaPath;
	if (false == TryGetMetaPathForAsset(oldAssetPath, oldMetaPath) || false == TryGetMetaPathForAsset(newAssetPath, newMetaPath))
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(oldMetaPath, errorCode) || std::filesystem::exists(newMetaPath, errorCode))
	{
		return false;
	}
	errorCode.clear();

	if (newMetaPath.has_parent_path())
	{
		std::filesystem::create_directories(newMetaPath.parent_path(), errorCode);
		errorCode.clear();
	}

	std::filesystem::rename(oldMetaPath, newMetaPath, errorCode);
	return false == static_cast<bool>(errorCode);
}

void CProjectManager::MarkAssetDatabaseChanged()
{
	++m_assetDatabaseRevision;
}

std::uint32_t CProjectManager::GetResolutionWidth() const
{
	return m_info.ResolutionWidth;
}

std::uint32_t CProjectManager::GetResolutionHeight() const
{
	return m_info.ResolutionHeight;
}

void CProjectManager::SetResolution(std::uint32_t width, std::uint32_t height)
{
	m_info.ResolutionWidth  = (width  > 0) ? width  : 1920;
	m_info.ResolutionHeight = (height > 0) ? height : 1080;
}

float CProjectManager::GetPixelsPerUnit() const
{
	return m_info.PixelsPerUnit;
}

void CProjectManager::SetPixelsPerUnit(float ppu)
{
	m_info.PixelsPerUnit = (ppu >= 1.0f) ? ppu : 100.0f;
}

float CProjectManager::GetSceneViewCamX() const
{
	return m_info.SceneViewCamX;
}

float CProjectManager::GetSceneViewCamY() const
{
	return m_info.SceneViewCamY;
}

float CProjectManager::GetSceneViewCamSize() const
{
	return m_info.SceneViewCamSize;
}

void CProjectManager::SetSceneViewCamera(float x, float y, float size)
{
	m_info.SceneViewCamX    = x;
	m_info.SceneViewCamY    = y;
	m_info.SceneViewCamSize = (size > 0.0f) ? size : 5.0f;
}

const std::string& CProjectManager::GetScriptDllPath() const
{
	return m_info.ScriptDllPath;
}

void CProjectManager::SetScriptDllPath(const std::string& path)
{
	m_info.ScriptDllPath = path;
}

const std::string& CProjectManager::GetScriptSourceDirectory() const
{
	return m_info.ScriptSourceDirectory;
}

void CProjectManager::SetScriptSourceDirectory(const std::string& path)
{
	(void)path;
	m_info.ScriptSourceDirectory = CONTENTS_DIRECTORY_NAME;
}

const std::string& CProjectManager::GetScriptBuildCommand() const
{
	return m_info.ScriptBuildCommand;
}

void CProjectManager::SetScriptBuildCommand(const std::string& command)
{
	m_info.ScriptBuildCommand = command;
}

const std::string& CProjectManager::GetScriptOutputLibraryPath() const
{
	return m_info.ScriptOutputLibraryPath;
}

void CProjectManager::SetScriptOutputLibraryPath(const std::string& path)
{
	m_info.ScriptOutputLibraryPath = path;
}

const std::string& CProjectManager::GetScriptIntermediateDirectory() const
{
	return m_info.ScriptIntermediateDirectory;
}

void CProjectManager::SetScriptIntermediateDirectory(const std::string& path)
{
	m_info.ScriptIntermediateDirectory = path;
}

EScriptBuildConfiguration CProjectManager::GetScriptBuildConfiguration() const
{
	return m_info.ScriptBuildConfiguration;
}

void CProjectManager::SetScriptBuildConfiguration(EScriptBuildConfiguration configuration)
{
	m_info.ScriptBuildConfiguration = configuration;
	m_info.ScriptOutputLibraryPath = std::string("x64/")
		+ ToString(configuration)
		+ "/GameScript.dll";
}

bool CProjectManager::IsScriptAutoRebuildEnabled() const
{
	return m_info.ScriptAutoRebuildEnabled;
}

void CProjectManager::SetScriptAutoRebuildEnabled(bool enabled)
{
	m_info.ScriptAutoRebuildEnabled = enabled;
}

bool CProjectManager::IsLiveCompileEnabled() const
{
	return IsScriptAutoRebuildEnabled();
}

void CProjectManager::SetLiveCompileEnabled(bool enabled)
{
	SetScriptAutoRebuildEnabled(enabled);
}

const std::string& CProjectManager::GetLastOpenedScenePath() const
{
	return m_info.LastOpenedScenePath;
}

void CProjectManager::SetLastOpenedScenePath(const std::string& relativePath)
{
	m_info.LastOpenedScenePath = relativePath;
}

const std::string& CProjectManager::GetEditorLocaleCode() const
{
	return m_info.EditorLocaleCode;
}

void CProjectManager::SetEditorLocaleCode(const std::string& localeCode)
{
	m_info.EditorLocaleCode = localeCode;
}

const std::string& CProjectManager::GetImGuiIniSettings() const
{
	return m_info.ImGuiIniSettings;
}

void CProjectManager::SetImGuiIniSettings(const std::string& iniSettings)
{
	m_info.ImGuiIniSettings = iniSettings;
}

bool CProjectManager::LoadScriptModule()
{
	if (false == m_isProjectLoaded || !m_scriptLoader || m_info.ScriptDllPath.empty())
	{
		return false;
	}

	// 이미 로드된 경우 먼저 언로드
	if (m_scriptLoader->IsLoaded())
	{
		m_scriptLoader->Unload();
	}

	// DLL 경로: 절대경로이면 그대로, 상대경로이면 프로젝트 루트 기준으로 결합
	std::filesystem::path dllPath(m_info.ScriptDllPath);
	if (false == dllPath.is_absolute() && false == m_info.RootPath.empty())
	{
		dllPath = m_info.RootPath / dllPath;
	}

	GameModuleContext context = BuildGameModuleContext();

	return m_scriptLoader->Load(dllPath, context);
}

void CProjectManager::UnloadScriptModule()
{
	if (m_engineCore && m_engineCore->SceneManager)
	{
		m_engineCore->SceneManager->DestroyScriptInstances();
	}
	if (m_scriptLoader)
	{
		m_scriptLoader->Unload();
	}
}

bool CProjectManager::IsScriptModuleLoaded() const
{
	return static_cast<bool>(m_scriptLoader) && m_scriptLoader->IsLoaded();
}

bool CProjectManager::StartLiveCompile()
{
	if (false == m_isProjectLoaded || !m_liveCompileManager)
	{
		return false;
	}

	m_info.ScriptAutoRebuildEnabled = true;
	if (false == m_liveCompileManager->Initialize(BuildLiveCompileDesc()))
	{
		CSystemLog::Error("Script live compile initialization failed.");
		return false;
	}

	const LiveCompileResult result = m_liveCompileManager->RebuildAndReload();
	if (false == result.Succeeded)
	{
		LogLiveCompileFailure("Script module load failed.", result);
		return false;
	}

	CSystemLog::Info("Script module loaded.");
	return true;
}

void CProjectManager::StopLiveCompile()
{
	m_info.ScriptAutoRebuildEnabled = false;
	if (m_liveCompileManager)
	{
		m_liveCompileManager->Finalize();
	}
}

bool CProjectManager::RebuildScriptModule()
{
	if (false == m_isProjectLoaded || !m_liveCompileManager)
	{
		return false;
	}

	if (ELiveCompileState::Idle == m_liveCompileManager->GetState()
		|| ELiveCompileState::Failed == m_liveCompileManager->GetState())
	{
		if (false == m_liveCompileManager->Initialize(BuildLiveCompileDesc()))
		{
			return false;
		}
	}

	LiveCompileResult result = m_liveCompileManager->RebuildAndReload();
	if (result.Succeeded)
	{
		CSystemLog::Info("Script module reloaded.");
	}
	else
	{
		LogLiveCompileFailure("Script module reload failed.", result);
	}
	return result.Succeeded;
}

bool CProjectManager::IsLiveCompileLoaded() const
{
	return m_liveCompileManager && ELiveCompileState::Loaded == m_liveCompileManager->GetState();
}

ELiveCompileState CProjectManager::GetLiveCompileState() const
{
	return m_liveCompileManager ? m_liveCompileManager->GetState() : ELiveCompileState::Idle;
}

std::string CProjectManager::ConsumeLastLiveCompileFailure()
{
	return m_liveCompileManager ? m_liveCompileManager->ConsumeLastFailureMessage() : std::string{};
}

GameModuleContext CProjectManager::BuildGameModuleContext() const
{
	GameModuleContext context;
	context.HostEngine = &Engine;
	return context;
}

LiveCompileDesc CProjectManager::BuildLiveCompileDesc() const
{
	LiveCompileDesc desc;
	const std::filesystem::path basePath = ResolveLiveCompileBasePath(m_info);
	const std::string outputLibraryPath = std::string("x64/")
		+ ToString(m_info.ScriptBuildConfiguration)
		+ "/GameScript.dll";
	desc.SourceDirectory = m_info.ScriptPath.string();
	desc.OutputLibraryPath = ResolveLiveCompilePath(basePath, outputLibraryPath).string();
	desc.IntermediateDirectory = ResolveLiveCompilePath(basePath, m_info.ScriptIntermediateDirectory).string();
	desc.BuildCommand = BuildDefaultLiveCompileCommand(m_info, basePath);
	desc.ModuleContext = BuildGameModuleContext();
	return desc;
}

// ── VS 연동 헬퍼 ──────────────────────────────────────────────────────────────
File::Path CProjectManager::FindScriptSolutionPath() const
{
	if (false == m_isProjectLoaded)
	{
		return File::NULL_PATH;
	}

	// 검색 후보: ScriptSourceDirectory, 그 부모, RootPath 순.
	const std::filesystem::path basePath = ResolveLiveCompileBasePath(m_info);
	const std::filesystem::path scriptDir = ResolveLiveCompilePath(basePath, m_info.ScriptSourceDirectory);

	const std::filesystem::path candidates[] = {
		scriptDir,
		scriptDir.parent_path(),
		basePath,
	};

	std::error_code errorCode;
	for (const std::filesystem::path& dir : candidates)
	{
		if (dir.empty() || false == std::filesystem::exists(dir, errorCode))
		{
			errorCode.clear();
			continue;
		}
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, errorCode))
		{
			if (errorCode)
			{
				errorCode.clear();
				break;
			}
			if (entry.is_regular_file(errorCode) && entry.path().extension() == ".sln")
			{
				return File::Path(entry.path());
			}
			errorCode.clear();
		}
	}
	return File::NULL_PATH;
}

File::Path CProjectManager::FindScriptVcxprojPath() const
{
	if (false == m_isProjectLoaded)
	{
		return File::NULL_PATH;
	}

	const std::filesystem::path basePath = ResolveLiveCompileBasePath(m_info);
	const std::filesystem::path vcxprojPath = ResolveLiveCompilePath(basePath, m_info.ScriptSourceDirectory) / "GameScript.vcxproj";
	std::error_code errorCode;
	if (std::filesystem::exists(vcxprojPath, errorCode))
	{
		return File::Path(vcxprojPath);
	}
	return File::NULL_PATH;
}

void CProjectManager::OpenScriptInIde(const File::Path& filePath) const
{
	// 1) 솔루션(.sln) 우선, 없으면 .vcxproj 로 폴백.  같은 솔루션을 반복 더블클릭할 때
	//    Visual Studio 인스턴스가 계속 늘어나는 것을 막기 위해 첫 호출에서만 연다.
	const File::Path slnPath = FindScriptSolutionPath();
	File::Path idePath = slnPath.empty() ? FindScriptVcxprojPath() : slnPath;
	if (idePath.empty() && filePath.empty())
	{
		return;
	}

	if (false == idePath.empty())
	{
		std::error_code errorCode;
		const std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(idePath, errorCode);
		const File::Path key = errorCode ? idePath : File::Path(normalizedPath);
		if (m_lastOpenedScriptIdePath != key)
		{
			m_lastOpenedScriptIdePath = key;
			File::OpenFile(idePath);
		}
	}

	// 2) 사용자가 특정 스크립트(.cpp/.h)를 더블클릭한 경우, 그 파일도 함께 열어 활성 탭으로.
	//    이미 솔루션이 열려 있으면 OS 가 같은 VS 인스턴스에서 새 탭으로 띄운다.
	if (false == filePath.empty())
	{
		std::error_code ec;
		if (std::filesystem::exists(filePath, ec))
		{
			File::OpenFile(filePath);
		}
	}
}

bool CProjectManager::SaveProject() const
{
	if (false == m_isProjectLoaded || m_info.ProjectFilePath.empty())
	{
		return false;
	}

	YAML::Emitter out;
	out << YAML::BeginMap;
	out << YAML::Key << PROJECT_KEY_VERSION         << YAML::Value << m_info.Version;
	out << YAML::Key << PROJECT_KEY_ROOT_PATH       << YAML::Value << ".";
	out << YAML::Key << PROJECT_KEY_RES_WIDTH       << YAML::Value << m_info.ResolutionWidth;
	out << YAML::Key << PROJECT_KEY_RES_HEIGHT      << YAML::Value << m_info.ResolutionHeight;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_X     << YAML::Value << m_info.SceneViewCamX;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_Y     << YAML::Value << m_info.SceneViewCamY;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_SIZE  << YAML::Value << m_info.SceneViewCamSize;
	out << YAML::Key << PROJECT_KEY_PIXELS_PER_UNIT << YAML::Value << m_info.PixelsPerUnit;
	out << YAML::Key << PROJECT_KEY_EDITOR_LOCALE   << YAML::Value << m_info.EditorLocaleCode;
	out << YAML::Key << PROJECT_KEY_SCRIPT_SOURCE_DIR << YAML::Value << m_info.ScriptSourceDirectory;
	out << YAML::Key << PROJECT_KEY_SCRIPT_BUILD_CMD << YAML::Value << m_info.ScriptBuildCommand;
	out << YAML::Key << PROJECT_KEY_SCRIPT_OUTPUT_DLL << YAML::Value << m_info.ScriptOutputLibraryPath;
	out << YAML::Key << PROJECT_KEY_SCRIPT_INTERMEDIATE << YAML::Value << m_info.ScriptIntermediateDirectory;
	out << YAML::Key << PROJECT_KEY_SCRIPT_BUILD_CONFIG << YAML::Value << ToString(m_info.ScriptBuildConfiguration);
	out << YAML::Key << PROJECT_KEY_SCRIPT_AUTO_REBUILD_ENABLED << YAML::Value << m_info.ScriptAutoRebuildEnabled;
	if (false == m_info.ScriptDllPath.empty())
	{
		out << YAML::Key << PROJECT_KEY_SCRIPT_DLL_PATH << YAML::Value << m_info.ScriptDllPath;
	}
	if (false == m_info.LastOpenedScenePath.empty())
	{
		out << YAML::Key << PROJECT_KEY_LAST_SCENE_PATH << YAML::Value << m_info.LastOpenedScenePath;
	}
	if (false == m_info.ImGuiIniSettings.empty())
	{
		out << YAML::Key << PROJECT_KEY_IMGUI_INI << YAML::Value << YAML::Literal << m_info.ImGuiIniSettings;
	}
	out << YAML::EndMap;

	std::ofstream file(m_info.ProjectFilePath, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return false;
	}
	file << out.c_str();
	return true;
}
