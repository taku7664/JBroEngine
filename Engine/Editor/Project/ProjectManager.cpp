#include "pch.h"
#include "ProjectManager.h"

#include "Core/Asset/AssetPath.h"
#include "Core/Asset/AssetMetaFile.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/IAssetRegistry.h"
#include "Core/EngineCore.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskGroup.h"
#include "Core/Task/Task.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Game/GameModuleTypes.h"
#include "Editor/Project/GameScriptProjectGenerator.h"
#include "Editor/LiveCompile/LiveCompileManager.h"
#include "Editor/ScriptModule/ScriptModuleLoader.h"
#include "GameFramework/Scene/SceneManager.h"
#include "GameFramework/Scene/SceneSerializer.h"
#include "yaml-cpp/yaml.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <unordered_set>

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
	constexpr const char* PROJECT_KEY_WATCH_IGNORE      = "AssetWatchIgnorePatterns";
	constexpr const char* PROJECT_KEY_BUILD             = "Build";
	constexpr const char* PROJECT_KEY_BUILD_PRODUCT_NAME = "ProductName";
	constexpr const char* PROJECT_KEY_BUILD_TARGET_PLATFORM = "TargetPlatform";
	constexpr const char* PROJECT_KEY_BUILD_CONFIGURATION = "BuildConfiguration";
	constexpr const char* PROJECT_KEY_BUILD_OUTPUT_DIR = "OutputDirectory";
	constexpr const char* PROJECT_KEY_BUILD_STARTUP_SCENE = "StartupScene";
	constexpr const char* PROJECT_KEY_BUILD_SCENES = "BuildScenes";
	constexpr const char* PROJECT_KEY_BUILD_SCRIPT_MODE = "ScriptMode";
	constexpr const char* PROJECT_KEY_BUILD_SCRIPT_PROJECT = "ScriptProjectPath";
	constexpr const char* PROJECT_KEY_BUILD_SCRIPT_CONFIG = "ScriptBuildConfiguration";
	constexpr const char* PROJECT_KEY_BUILD_SCRIPT_OUTPUT = "ScriptOutputLibraryPath";
	constexpr const char* PROJECT_KEY_BUILD_WINDOWS_ICON_GUID = "WindowsIconGuid";
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

	EBuildTargetPlatform ParseBuildTargetPlatform(const std::string& value)
	{
		if (value == "Web") return EBuildTargetPlatform::Web;
		if (value == "Android") return EBuildTargetPlatform::Android;
		if (value == "IOS") return EBuildTargetPlatform::IOS;
		return EBuildTargetPlatform::Windows;
	}

	const char* ToString(EBuildTargetPlatform platform)
	{
		switch (platform)
		{
		case EBuildTargetPlatform::Web: return "Web";
		case EBuildTargetPlatform::Android: return "Android";
		case EBuildTargetPlatform::IOS: return "IOS";
		default: return "Windows";
		}
	}

	EBuildConfiguration ParseBuildConfiguration(const std::string& value)
	{
		if (value == "Debug") return EBuildConfiguration::Debug;
		return EBuildConfiguration::Release;
	}

	const char* ToString(EBuildConfiguration configuration)
	{
		return EBuildConfiguration::Debug == configuration ? "Debug" : "Release";
	}

	EBuildScriptMode ParseBuildScriptMode(const std::string& value)
	{
		if (value == "Static") return EBuildScriptMode::Static;
		return EBuildScriptMode::DynamicLibrary;
	}

	const char* ToString(EBuildScriptMode mode)
	{
		return EBuildScriptMode::Static == mode ? "Static" : "DynamicLibrary";
	}

	ProjectBuildSettings MakeDefaultBuildSettings(const std::filesystem::path& projectPath, const std::string& startupScene)
	{
		ProjectBuildSettings settings;
		settings.ProductName = projectPath.stem().string();
		settings.TargetPlatform = EBuildTargetPlatform::Windows;
		settings.BuildConfiguration = EBuildConfiguration::Release;
		settings.OutputDirectory = "Dist/Games";
		settings.StartupScene = startupScene;
		if (false == startupScene.empty())
		{
			settings.BuildScenes.push_back(startupScene);
		}
		settings.ScriptMode = EBuildScriptMode::DynamicLibrary;
		settings.ScriptProjectPath = std::string(CONTENTS_DIRECTORY_NAME) + "/GameScript.vcxproj";
		settings.ScriptBuildConfiguration = EScriptBuildConfiguration::Release;
		settings.ScriptOutputLibraryPath = "GameScript.dll";
		settings.WindowsIconGuid = INVALID_ASSET_GUID;
		return settings;
	}

	void NormalizeBuildSettings(ProjectBuildSettings& settings, const std::filesystem::path& projectPath)
	{
		if (settings.ProductName.empty())
		{
			settings.ProductName = projectPath.stem().string();
		}
		if (settings.OutputDirectory.empty())
		{
			settings.OutputDirectory = "Dist/Games";
		}
		if (settings.ScriptProjectPath.empty())
		{
			settings.ScriptProjectPath = std::string(CONTENTS_DIRECTORY_NAME) + "/GameScript.vcxproj";
		}
		settings.ScriptBuildConfiguration = EBuildConfiguration::Debug == settings.BuildConfiguration
			? EScriptBuildConfiguration::Debug
			: EScriptBuildConfiguration::Release;
		if (settings.ScriptOutputLibraryPath.empty())
		{
			settings.ScriptOutputLibraryPath = EBuildScriptMode::DynamicLibrary == settings.ScriptMode
				? "GameScript.dll"
				: "";
		}
		if (settings.BuildScenes.empty() && false == settings.StartupScene.empty())
		{
			settings.BuildScenes.push_back(settings.StartupScene);
		}
		if (false == settings.StartupScene.empty()
			&& std::find(settings.BuildScenes.begin(), settings.BuildScenes.end(), settings.StartupScene) == settings.BuildScenes.end())
		{
			settings.BuildScenes.insert(settings.BuildScenes.begin(), settings.StartupScene);
		}
		if (EBuildTargetPlatform::Windows != settings.TargetPlatform)
		{
			settings.ScriptMode = EBuildScriptMode::Static;
			settings.ScriptProjectPath.clear();
			settings.ScriptOutputLibraryPath.clear();
		}
		else
		{
			settings.ScriptMode = EBuildScriptMode::DynamicLibrary;
			settings.ScriptProjectPath = std::string(CONTENTS_DIRECTORY_NAME) + "/GameScript.vcxproj";
			settings.ScriptOutputLibraryPath = "GameScript.dll";
		}
	}

	ProjectBuildSettings ReadBuildSettings(const YAML::Node& root, const std::filesystem::path& projectPath, const std::string& startupScene)
	{
		ProjectBuildSettings settings = MakeDefaultBuildSettings(projectPath, startupScene);
		const YAML::Node buildNode = root[PROJECT_KEY_BUILD];
		if (false == static_cast<bool>(buildNode) || false == buildNode.IsMap())
		{
			return settings;
		}

		settings.ProductName = buildNode[PROJECT_KEY_BUILD_PRODUCT_NAME].as<std::string>(settings.ProductName);
		settings.TargetPlatform = ParseBuildTargetPlatform(buildNode[PROJECT_KEY_BUILD_TARGET_PLATFORM].as<std::string>(ToString(settings.TargetPlatform)));
		settings.BuildConfiguration = ParseBuildConfiguration(buildNode[PROJECT_KEY_BUILD_CONFIGURATION].as<std::string>(ToString(settings.BuildConfiguration)));
		settings.OutputDirectory = buildNode[PROJECT_KEY_BUILD_OUTPUT_DIR].as<std::string>(settings.OutputDirectory);
		settings.StartupScene = buildNode[PROJECT_KEY_BUILD_STARTUP_SCENE].as<std::string>(settings.StartupScene);
		settings.ScriptMode = ParseBuildScriptMode(buildNode[PROJECT_KEY_BUILD_SCRIPT_MODE].as<std::string>(ToString(settings.ScriptMode)));
		settings.ScriptProjectPath = buildNode[PROJECT_KEY_BUILD_SCRIPT_PROJECT].as<std::string>(settings.ScriptProjectPath);
		settings.ScriptBuildConfiguration = ParseScriptBuildConfiguration(buildNode[PROJECT_KEY_BUILD_SCRIPT_CONFIG].as<std::string>(ToString(settings.ScriptBuildConfiguration)));
		settings.ScriptOutputLibraryPath = buildNode[PROJECT_KEY_BUILD_SCRIPT_OUTPUT].as<std::string>(settings.ScriptOutputLibraryPath);
		settings.WindowsIconGuid = AssetGuid(buildNode[PROJECT_KEY_BUILD_WINDOWS_ICON_GUID].as<std::string>(settings.WindowsIconGuid.generic_string()));

		settings.BuildScenes.clear();
		const YAML::Node scenesNode = buildNode[PROJECT_KEY_BUILD_SCENES];
		if (scenesNode && scenesNode.IsSequence())
		{
			for (const YAML::Node& sceneNode : scenesNode)
			{
				const std::string scene = sceneNode.as<std::string>("");
				if (false == scene.empty())
				{
					settings.BuildScenes.push_back(scene);
				}
			}
		}

		NormalizeBuildSettings(settings, projectPath);
		return settings;
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

	// glob 매칭 — '*'(0+ 문자), '?'(임의 1 문자). 백트래킹 단순 구현.
	// 패턴은 보통 짧고(파일명 수준) 자산 import 핫패스가 아니므로 단순 재귀로 충분.
	bool GlobMatch(const char* pattern, const char* text)
	{
		while (*pattern)
		{
			if ('*' == *pattern)
			{
				while ('*' == *(pattern + 1)) ++pattern;        // 연속 '*' 압축
				if ('\0' == *(pattern + 1)) return true;        // 패턴 끝의 '*' — 무엇이든 매칭
				for (const char* t = text; *t; ++t)
				{
					if (GlobMatch(pattern + 1, t)) return true;
				}
				return GlobMatch(pattern + 1, text);            // text 끝까지 와도 '*' 가 빈 매칭 가능
			}
			if ('\0' == *text) return false;
			if ('?' != *pattern && *pattern != *text) return false;
			++pattern; ++text;
		}
		return '\0' == *text;
	}

	// 한 경로가 패턴 집합 중 하나에라도 매칭되는지. 패턴은 파일명 또는 (슬래시 포함 시) 상대경로에 매칭.
	bool MatchAnyPattern(const std::vector<std::string>& patterns, const std::string& fileName, const std::string& relativePath)
	{
		for (const std::string& pattern : patterns)
		{
			if (pattern.empty()) continue;
			// 슬래시가 들어있는 패턴은 상대경로 전체에, 아니면 파일명에 매칭.
			const std::string& target = (pattern.find('/') != std::string::npos) ? relativePath : fileName;
			if (GlobMatch(pattern.c_str(), target.c_str())) return true;
		}
		return false;
	}
}

bool CProjectManager::Initialize(const EngineCore& context)
{
	m_assetManager  = context.AssetManager;
	m_assetWatcher  = MakeOwnerPtr<CWindowsFileWatcher>();
	m_scriptLoader  = MakeOwnerPtr<CScriptModuleLoader>();
	m_liveCompileManager = MakeOwnerPtr<CLiveCompileManager>();
	// 매 빌드 직전에 스크립트 프로젝트(레지스트리/vcxproj)를 재생성해, 헤더를 어떻게
	// 편집하든(프로퍼티 추가/이름변경/삭제, 외부 파일 추가/삭제) 항상 디스크와 동기화.
	if (m_liveCompileManager)
	{
		m_liveCompileManager->SetPreBuildCallback([this]() { RegenerateScriptProject(); });
	}
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
		ProjectBuildSettings buildSettings = MakeDefaultBuildSettings(projectFile, "");
		out << YAML::Key << PROJECT_KEY_BUILD << YAML::Value;
		out << YAML::BeginMap;
		out << YAML::Key << PROJECT_KEY_BUILD_PRODUCT_NAME << YAML::Value << buildSettings.ProductName;
		out << YAML::Key << PROJECT_KEY_BUILD_TARGET_PLATFORM << YAML::Value << ToString(buildSettings.TargetPlatform);
		out << YAML::Key << PROJECT_KEY_BUILD_CONFIGURATION << YAML::Value << ToString(buildSettings.BuildConfiguration);
		out << YAML::Key << PROJECT_KEY_BUILD_OUTPUT_DIR << YAML::Value << buildSettings.OutputDirectory;
		out << YAML::Key << PROJECT_KEY_BUILD_STARTUP_SCENE << YAML::Value << buildSettings.StartupScene;
		out << YAML::Key << PROJECT_KEY_BUILD_SCENES << YAML::Value << YAML::BeginSeq << YAML::EndSeq;
		out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_MODE << YAML::Value << ToString(buildSettings.ScriptMode);
		out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_PROJECT << YAML::Value << buildSettings.ScriptProjectPath;
		out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_CONFIG << YAML::Value << ToString(buildSettings.ScriptBuildConfiguration);
		out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_OUTPUT << YAML::Value << buildSettings.ScriptOutputLibraryPath;
		out << YAML::Key << PROJECT_KEY_BUILD_WINDOWS_ICON_GUID << YAML::Value << buildSettings.WindowsIconGuid.generic_string();
		out << YAML::EndMap;
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

	// AssetWatchIgnorePatterns — 키가 있으면 그 값으로 덮어쓰고, 없으면 ProjectInfo 의 기본값(임시파일 패턴) 유지.
	std::vector<std::string> assetWatchIgnorePatterns;
	bool hasAssetWatchIgnorePatterns = false;
	if (root[PROJECT_KEY_WATCH_IGNORE] && root[PROJECT_KEY_WATCH_IGNORE].IsSequence())
	{
		hasAssetWatchIgnorePatterns = true;
		for (const YAML::Node& patternNode : root[PROJECT_KEY_WATCH_IGNORE])
		{
			std::string pattern = patternNode.as<std::string>("");
			// 외부 편집/CRLF 혼선 대비 — 트레일링 \r, 양끝 공백 트림.
			while (false == pattern.empty() && (pattern.back() == '\r' || pattern.back() == ' ' || pattern.back() == '\t')) pattern.pop_back();
			while (false == pattern.empty() && (pattern.front() == ' ' || pattern.front() == '\t')) pattern.erase(pattern.begin());
			if (false == pattern.empty()) assetWatchIgnorePatterns.push_back(std::move(pattern));
		}
	}

	float pixelsPerUnit = 100.0f;
	if (root[PROJECT_KEY_PIXELS_PER_UNIT])
	{
		pixelsPerUnit = root[PROJECT_KEY_PIXELS_PER_UNIT].as<float>(100.0f);
		if (pixelsPerUnit < 1.0f) pixelsPerUnit = 1.0f;
	}

	ProjectBuildSettings buildSettings = ReadBuildSettings(root, projectPath, lastOpenedScenePath);

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
	m_info.BuildSettings       = buildSettings;
	m_info.PixelsPerUnit       = pixelsPerUnit;
	m_info.EditorLocaleCode    = editorLocaleCode;
	m_info.ImGuiIniSettings    = imguiIniSettings;
	if (hasAssetWatchIgnorePatterns)
	{
		m_info.AssetWatchIgnorePatterns = std::move(assetWatchIgnorePatterns);
	}
	// else: ProjectInfo 의 기본값(*.tmp 등 임시 파일 패턴) 그대로 유지.

	if (false == m_assetManager->SetAssetRootPath(m_info.AssetPath))
	{
		return false;
	}

	CGameScriptProjectGenerator gameScriptProjectGenerator;
	if (false == gameScriptProjectGenerator.EnsureProject(m_info))
	{
		return false;
	}

	// ── 자산 정합성 패스(reconcile) — 데이터는 메모리에 올리지 않는다 ─────────
	// Assets 트리를 1회 스캔하며 디스크 실제 상태 ↔ 레지스트리를 일관·치유 상태로
	// 맞춘다: 메타 없는 raw 는 생성, 분실/이동된 메타는 복구 캐시(해시)로 같은 GUID
	// 복구, 중복 GUID 는 결정적으로 재발급, 고아 메타는 격리. 모두 로그로 남긴다.
	// 텍스처 디코드/GPU 업로드는 없다(데이터 로드는 아래 "마지막 씬 참조 에셋"만).
	// 파일 워처는 reconcile 이후에 Watch 한다 — reconcile 이 생성/격리한 메타가
	// 초기 스냅샷에 이미 반영되어 불필요한(자기-유발) 이벤트가 안 생기도록.
	m_assetDb.Load(GetAssetDbPath());
	m_lastReconcileReport = ReconcileAssets();
	{
		const AssetReconcileReport& r = m_lastReconcileReport;
		CSystemLog::Info("[AssetReconcile] registered=" + std::to_string(r.Registered)
			+ " generated=" + std::to_string(r.MetaGenerated)
			+ " recovered=" + std::to_string(r.GuidRecovered)
			+ " relinked=" + std::to_string(r.Relinked)
			+ " dupResolved=" + std::to_string(r.DuplicateResolved)
			+ " orphanQuarantined=" + std::to_string(r.OrphanQuarantined)
			+ " failed=" + std::to_string(r.Failed));
	}
	errorCode.clear();

	FileWatcherDesc watcherDesc;
	watcherDesc.RootPath = m_info.AssetPath;
	watcherDesc.Recursive = true;
	m_assetWatcher->Watch(watcherDesc);

	// 로케일 / ImGui ini 같은 빠른 메인-스레드 후처리는 즉시 수행 (자산 로드와 독립).
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

	// ── 동기 폴백용 헬퍼 — Task 경로가 아닐 때 메인 스레드에서 직접 실행 ──
	auto runScriptBuildSync = [this]()
	{
		if (false == static_cast<bool>(m_liveCompileManager)) return;
		if (false == m_liveCompileManager->Initialize(BuildLiveCompileDesc()))
		{
			CSystemLog::Error("Script live compile initialization failed.");
			return;
		}
		const LiveCompileResult result = m_liveCompileManager->RebuildAndReload();
		if (result.Succeeded) CSystemLog::Info("Script module loaded.");
		else                  LogLiveCompileFailure("Script module load failed.", result);
	};

	// 모든 자산 임포트가 완료된 뒤 메인 스레드에서 실행될 후처리. DLL 빌드/로드는
	// 별도 워커 태스크로 분리되므로 여기는 MarkAssetDatabaseChanged 만 남는다.
	m_postLoadAction = [this]()
	{
		MarkAssetDatabaseChanged();
	};

	// 프로젝트 자체는 로드된 상태로 표시 — 인스펙터/AssetBrowser 등이 점진적으로 채워진다.
	m_isProjectLoaded = true;

	// 자산(스프라이트) 폴백 PPU 를 런타임 측에서도 보이게. 스프라이트가 처음 렌더되기 전에 채운다.
	Engine.PixelsPerUnit = m_info.PixelsPerUnit;

	// ── 마지막 씬이 참조하는 에셋만 수집 (프리팹 참조까지 전이적으로 확장) ──
	// 씬 파일의 ReferencedAssets 목록을 기반으로, 프리팹이면 그 프리팹의 참조까지
	// 펼쳐 "이 씬을 띄우는 데 실제로 필요한" 자산 GUID 들만 모은다.
	const std::vector<AssetGuid> sceneAssets = CollectSceneLoadAssets(m_info.LastOpenedScenePath);

	// ── 동기 폴백 — TaskManager/Group 이 없을 때 메인 스레드에서 직접 로드 ──
	auto runSyncFallback = [this, &sceneAssets, &runScriptBuildSync]()
	{
		if (m_assetManager.IsValid())
		{
			for (const AssetGuid& guid : sceneAssets)
			{
				m_assetManager->LoadAsset(guid);
			}
		}
		runScriptBuildSync();
		if (m_postLoadAction) { auto act = std::move(m_postLoadAction); m_postLoadAction = nullptr; act(); }
	};

	SafePtr<CTaskManager> taskManager = (m_engineCore && m_engineCore->TaskManager.IsValid()) ? m_engineCore->TaskManager : nullptr;
	if (false == taskManager.IsValid())
	{
		runSyncFallback();
		return true;
	}

	SafePtr<CTaskGroup> group = taskManager->CreateTaskGroup("ProjectLoad");
	if (false == group.IsValid())
	{
		runSyncFallback();
		return true;
	}

	SafePtr<CProjectManager> selfRef = SafeFromThis();

	// 에셋 로드 태스크 개수 = min(5, 참조 에셋 수).  씬이 참조하는 에셋을 ≤5 개의
	// 청크로 분배해 각 청크를 하나의 로드 태스크로 만든다. 스크립트 빌드 태스크 1개 추가.
	const std::size_t assetCount     = sceneAssets.size();
	const std::size_t assetTaskCount = (0 == assetCount) ? 0 : (assetCount < 5 ? assetCount : 5);
	const std::size_t totalTasks     = assetTaskCount + (m_liveCompileManager ? 1u : 0u);

	// 로드할 게 전혀 없으면(씬도 스크립트도 없음) 그룹을 만들지 않고 즉시 후처리.
	if (0 == totalTasks)
	{
		runSyncFallback();
		return true;
	}

	m_loadTaskGroup = group;

	// 의도적으로 group->AllCompletedCallback 를 쓰지 않는다 — task 를 큐잉하는 도중
	// 빠른 task 가 먼저 끝나면 자동 콜백이 조기 발화할 수 있어서, 총 개수를 미리
	// 알고 자체 카운터로 마지막 완료를 판정한다.
	auto completedCounter = std::make_shared<std::atomic<std::size_t>>(0);
	auto onTaskFinished = [selfRef, completedCounter, totalTasks]()
	{
		// task->EndCallback 은 TaskManager 가 PostMainThreadTask 로 메인 스레드에서 호출.
		const std::size_t completed = ++(*completedCounter);
		if (completed < totalTasks) return;

		CProjectManager* self = selfRef.TryGet();
		if (nullptr == self) return;
		self->m_loadTaskGroup = nullptr;
		if (self->m_postLoadAction)
		{
			auto act = std::move(self->m_postLoadAction);
			self->m_postLoadAction = nullptr;
			act();
		}
	};

	// ── 씬 참조 에셋 로드 — 최대 5 개의 태스크로 분배 ────────────────────
	if (assetTaskCount > 0)
	{
		const std::string loadAssetsLabel = (m_engineCore && m_engineCore->Localization.IsValid())
			? m_engineCore->Localization->Text("project.loading.task.load_assets")
			: std::string("Loading Resources");

		const std::size_t baseChunk  = assetCount / assetTaskCount;
		const std::size_t remainder  = assetCount % assetTaskCount;
		std::size_t cursor = 0;
		for (std::size_t t = 0; t < assetTaskCount; ++t)
		{
			const std::size_t chunkSize = baseChunk + (t < remainder ? 1u : 0u);
			std::vector<AssetGuid> chunk(sceneAssets.begin() + cursor, sceneAssets.begin() + cursor + chunkSize);
			cursor += chunkSize;

			// 작업 설명 = "리소스 로드 (i/N)" — 진행률 팝업의 작업 목록에 표시된다.
			std::string taskDescription = loadAssetsLabel + " (" + std::to_string(t + 1) + "/" + std::to_string(assetTaskCount) + ")";
			SafePtr<CTask> task = group->CreateTask("LoadSceneAssets", [selfRef, chunk]()
			{
				CProjectManager* self = selfRef.TryGet();
				if (nullptr == self || false == self->m_assetManager.IsValid()) return;
				for (const AssetGuid& guid : chunk)
				{
					self->m_assetManager->LoadAsset(guid);
				}
			}, taskDescription.c_str());
			if (task.IsValid()) task->EndCallback = onTaskFinished;
		}
	}

	// ── 스크립트 DLL 빌드/로드 — 별도 워커 태스크 (에셋 로드와 병렬) ──────
	// LiveCompileManager 의 Initialize/RebuildAndReload 가 워커 단일 호출 — 메인
	// 스레드 Tick 은 HasLoadingTasks 동안 보류되어 동시 접근을 차단한다.
	if (m_liveCompileManager)
	{
		const std::string scriptBuildDescription = (m_engineCore && m_engineCore->Localization.IsValid())
			? m_engineCore->Localization->Text("project.loading.task.script_build")
			: std::string("Building Scripts");
		SafePtr<CTask> task = group->CreateTask("ScriptBuild", [selfRef]()
		{
			if (false == selfRef.IsValid()) return;
			CProjectManager* self = selfRef.TryGet();
			if (nullptr == self || false == static_cast<bool>(self->m_liveCompileManager)) return;

			if (false == self->m_liveCompileManager->Initialize(self->BuildLiveCompileDesc()))
			{
				CSystemLog::Error("Script live compile initialization failed.");
				return;
			}
			const LiveCompileResult result = self->m_liveCompileManager->RebuildAndReload();
			if (result.Succeeded)
			{
				CSystemLog::Info("Script module loaded.");
			}
			else
			{
				LogLiveCompileFailure("Script module load failed.", result);
			}
		}, scriptBuildDescription.c_str());
		if (task.IsValid()) task->EndCallback = onTaskFinished;
	}

	return true;
}

void CProjectManager::CloseProject()
{
	// 복구 캐시를 디스크에 보존(다음 로드의 GUID 복구 힌트). 그 뒤 메모리 캐시 비움.
	if (m_isProjectLoaded && m_assetDbDirty)
	{
		m_assetDb.Save(GetAssetDbPath());
	}
	m_assetDb.Clear();
	m_assetDbDirty = false;
	m_lastReconcileReport = AssetReconcileReport{};

	// 진행 중이던 자산 로드 후크 해제. 워커는 마저 완료되지만 (mutex 로 안전), 후처리 콜백은 호출되지 않는다.
	m_postLoadAction = nullptr;
	m_loadTaskGroup  = nullptr;

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
	m_info.BuildSettings = ProjectBuildSettings{};
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

	// 비동기 자산 로드 / 스크립트 빌드 태스크가 진행 중이면 메인-스레드 Tick 을 잠시 보류.
	// LiveCompileManager 가 워커 스레드에서 Initialize/Rebuild 하는 동안 메인 스레드에서
	// Tick (자동 리빌드 폴링) 이 동시 호출되면 내부 상태가 깨질 수 있어 회피.
	if (HasLoadingTasks())
	{
		return;
	}

	// 파일 워처는 스냅샷-diff(전체 트리 walk)라 매 프레임 폴링하면 대형 프로젝트에서
	// 비싸다. 약 0.4s 간격으로만 폴링한다(외부 탐색기 동작 반영엔 충분히 빠르다).
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now - m_lastWatchPollTime >= std::chrono::milliseconds(400))
	{
		m_lastWatchPollTime = now;
		m_assetWatcher->Poll();
		std::vector<FileWatchEvent> events;
		if (m_assetWatcher->TakeEvents(events))
		{
			ProcessAssetEvents(events);
		}
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

bool CProjectManager::HasLoadingTasks() const
{
	return m_loadTaskGroup.IsValid() && false == m_loadTaskGroup->IsCompleted();
}

float CProjectManager::GetLoadProgress01() const
{
	return m_loadTaskGroup.IsValid() ? m_loadTaskGroup->GetProgress01() : 1.0f;
}

std::uint32_t CProjectManager::GetLoadCompletedCount() const
{
	return m_loadTaskGroup.IsValid() ? m_loadTaskGroup->GetCompletedTaskCount() : 0u;
}

std::uint32_t CProjectManager::GetLoadTotalCount() const
{
	return m_loadTaskGroup.IsValid() ? m_loadTaskGroup->GetTotalTaskCount() : 0u;
}

std::vector<TaskProgressInfo> CProjectManager::GetLoadTaskSnapshot() const
{
	return m_loadTaskGroup.IsValid() ? m_loadTaskGroup->GetTaskProgressSnapshot() : std::vector<TaskProgressInfo>{};
}

std::vector<AssetGuid> CProjectManager::CollectSceneLoadAssets(const std::string& sceneRelativePath) const
{
	std::vector<AssetGuid> result;
	if (sceneRelativePath.empty() || false == m_assetManager.IsValid())
	{
		return result;
	}

	std::error_code errorCode;
	const std::filesystem::path sceneFile = m_info.AssetPath / sceneRelativePath;
	if (false == std::filesystem::exists(sceneFile, errorCode))
	{
		return result;
	}

	CSceneSerializer serializer;
	const IAssetRegistry& registry = m_assetManager->GetRegistry();

	// 씬 파일의 ReferencedAssets 를 시작점으로, 프리팹이면 그 프리팹 파일의
	// ReferencedAssets 까지 BFS/DFS 로 전이 확장한다. (프리팹도 같은 씬 직렬화
	// 포맷이라 ReferencedAssets 블록을 그대로 가진다.)
	std::unordered_set<AssetGuid> visited;
	std::vector<AssetGuid> pending = serializer.ReadReferencedAssetsFromFile(File::Path(sceneFile.generic_string()));

	while (false == pending.empty())
	{
		const AssetGuid guid = pending.back();
		pending.pop_back();
		if (guid.IsNull() || false == visited.insert(guid).second)
		{
			continue;
		}
		result.push_back(guid);

		// 프리팹이면 그 프리팹이 참조하는 에셋도 펼친다.
		const AssetMetaData* meta = registry.FindAsset(guid);
		if (nullptr != meta && EAssetType::Prefab == meta->Type && false == meta->Path.empty())
		{
			const std::filesystem::path prefabFile = m_info.AssetPath / meta->Path;
			if (std::filesystem::exists(prefabFile, errorCode))
			{
				for (const AssetGuid& nested : serializer.ReadReferencedAssetsFromFile(File::Path(prefabFile.generic_string())))
				{
					pending.push_back(nested);
				}
			}
			errorCode.clear();
		}
	}

	return result;
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

const AssetReconcileReport& CProjectManager::GetLastReconcileReport() const
{
	return m_lastReconcileReport;
}

File::Path CProjectManager::GetAssetDbPath() const
{
	return m_info.RootPath / File::Path("Intermediate") / File::Path("AssetDb.yaml");
}

const char* CProjectManager::ImporterNameForType(EAssetType type)
{
	// 인스펙터 "임포터" 필드에 그대로 표시되므로 "Default" 대신 타입명을 사용.
	switch (type)
	{
	case EAssetType::Sprite:   return "Sprite";
	case EAssetType::Audio:    return "Audio";
	case EAssetType::Material: return "Material";
	case EAssetType::Shader:   return "Shader";
	case EAssetType::Scene:    return "Scene";
	case EAssetType::Prefab:   return "Prefab";
	case EAssetType::Script:   return "Script";
	case EAssetType::Mesh:     return "Mesh";
	default:                   return "Default";
	}
}

AssetReconcileReport CProjectManager::ReconcileAssets()
{
	AssetReconcileReport report;
	if (false == m_assetManager.IsValid())
	{
		return report;
	}

	IAssetRegistry& registry = m_assetManager->GetRegistry();
	registry.ClearNonPersistent();   // 프로젝트 자산은 reconcile 결과로 새로 채운다.

	std::error_code ec;
	const std::filesystem::path assetRoot(m_info.AssetPath);

	struct RawFile { File::Path Abs; std::string Rel; EAssetType Type; };
	std::vector<RawFile> rawFiles;
	std::unordered_set<std::string> rawRelSet;
	std::vector<std::pair<File::Path, std::string>> metaCandidates;   // (absMeta, assetRel)

	// ── 1) 트리 스캔: raw 와 사이드카 메타를 수집 (한 파일 오류는 건너뜀) ─────
	for (std::filesystem::recursive_directory_iterator it(assetRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
		it != end;
		it.increment(ec))
	{
		if (ec) { ec.clear(); continue; }
		if (false == it->is_regular_file(ec)) { ec.clear(); continue; }
		ec.clear();

		const File::Path abs(it->path());
		std::string rel;
		if (false == MakeAssetRelativePath(abs, rel))
		{
			continue;
		}

		if (CAssetPath::IsMetaPath(rel.c_str()))
		{
			metaCandidates.emplace_back(abs, CAssetPath::StripMetaExtension(rel.c_str()));
			continue;
		}

		RawFile rf;
		rf.Abs  = abs;
		rf.Rel  = rel;
		rf.Type = DetectAssetType(File::Path(rel));
		rawFiles.push_back(std::move(rf));
		rawRelSet.insert(rel);
	}

	// 경로 오름차순 정렬 — 중복 GUID 해소 시 "사전순 첫 경로"가 원본 GUID 를 유지하도록
	// 결정적으로 만든다(디렉터리 순회 순서에 따라 매 실행 결과가 뒤바뀌지 않게).
	std::sort(rawFiles.begin(), rawFiles.end(),
		[](const RawFile& a, const RawFile& b) { return a.Rel < b.Rel; });

	// ── 2) raw 등록: 메타 로드 → GUID 해소(중복/복구) → 메타 보정 → 등록 → DB 갱신 ─
	std::unordered_set<AssetGuid> seenGuids;

	for (const RawFile& rf : rawFiles)
	{
		// 콘텐츠 해시 — size+mtime 이 DB 와 같으면 재해싱 생략(증분).
		std::uint64_t size = 0, hash = 0;
		std::int64_t  mtime = 0;
		const bool haveStat = CAssetDatabase::QueryStat(rf.Abs, size, mtime);
		if (const AssetDbEntry* byPath = m_assetDb.FindByPath(rf.Rel);
			haveStat && byPath && byPath->ContentHash != 0 && byPath->FileSize == size && byPath->ModifiedTime == mtime)
		{
			hash = byPath->ContentHash;
		}
		else
		{
			std::uint64_t h = 0, s = 0; std::int64_t m = 0;
			if (CAssetDatabase::HashFile(rf.Abs, h, s, m)) { hash = h; size = s; mtime = m; }
		}

		// 사이드카 메타 로드
		File::Path metaPath;
		TryGetMetaPathForAsset(rf.Abs, metaPath);
		AssetMetaData meta;
		const bool haveMeta = (false == metaPath.empty())
			&& std::filesystem::exists(metaPath, ec)
			&& CAssetMetaFile::Load(metaPath, meta);
		ec.clear();

		bool needWriteMeta = false;
		bool generated = false, recovered = false, relinked = false, dupResolved = false;

		if (haveMeta && false == meta.Guid.IsNull())
		{
			if (seenGuids.count(meta.Guid) > 0)
			{
				// 중복 GUID — 조용히 누락하지 않고 결정적으로 새 GUID 재발급 + 메타 재작성.
				CSystemLog::Warning("[AssetReconcile] duplicate GUID resolved for: " + rf.Rel);
				meta.Guid     = CAssetPath::GenerateAssetGuid();
				needWriteMeta = true;
				dupResolved   = true;
			}
		}
		else
		{
			// 메타 없음/손상 → 복구 캐시로 같은 GUID 복구(해시 우선, 경로 보조).
			const AssetDbEntry* rec = (hash != 0) ? m_assetDb.FindByHash(hash) : nullptr;
			if (nullptr == rec) rec = m_assetDb.FindByPath(rf.Rel);

			meta = AssetMetaData{};
			if (nullptr != rec && false == rec->Guid.IsNull() && seenGuids.count(rec->Guid) == 0)
			{
				meta.Guid              = rec->Guid;
				meta.Type              = (EAssetType::Unknown != rec->Type) ? rec->Type : rf.Type;
				meta.Version           = rec->Version ? rec->Version : 1u;
				meta.DisplayName       = rec->DisplayName;
				meta.Importer          = rec->Importer;
				meta.ImportOptionsYaml = rec->ImportOptionsYaml;
				if (rec->RelativePath != rf.Rel) { relinked = true; }   // 이동 복구
				else                              { recovered = true; } // 제자리 메타 분실 복구
			}
			else
			{
				meta.Guid    = CAssetPath::GenerateAssetGuid();
				meta.Type    = rf.Type;
				meta.Version = 1u;
				generated    = true;
			}
			needWriteMeta = true;
		}

		// 메타 필드 보정
		if (EAssetType::Unknown == meta.Type)   meta.Type = rf.Type;
		if (0 == meta.Version)                   meta.Version = 1u;
		if (meta.DisplayName.empty())            meta.DisplayName = CAssetPath::GetDisplayNameFromPath(rf.Rel.c_str());
		if (meta.Importer.empty())               meta.Importer = ImporterNameForType(meta.Type);

		meta.Path     = File::Path(rf.Rel);
		meta.MetaPath = File::Path(CAssetPath::MakeMetaPath(rf.Rel.c_str()));

		if (needWriteMeta && false == metaPath.empty())
		{
			CAssetMetaFile::Save(metaPath, meta);
		}

		if (false == registry.RegisterAsset(meta))
		{
			report.Failed++;
			continue;
		}
		seenGuids.insert(meta.Guid);
		report.Registered++;
		if (generated)   report.MetaGenerated++;
		if (recovered)   report.GuidRecovered++;
		if (relinked)    report.Relinked++;
		if (dupResolved) report.DuplicateResolved++;

		AssetDbEntry e;
		e.Guid              = meta.Guid;
		e.RelativePath      = rf.Rel;
		e.ContentHash       = hash;
		e.FileSize          = size;
		e.ModifiedTime      = mtime;
		e.Type              = meta.Type;
		e.Version           = meta.Version;
		e.DisplayName       = meta.DisplayName;
		e.Importer          = meta.Importer;
		e.ImportOptionsYaml = meta.ImportOptionsYaml;
		m_assetDb.Upsert(e);
	}

	// ── 3) 고아 메타(raw 없음) 격리 ─────────────────────────────────────────
	for (const auto& pair : metaCandidates)
	{
		if (rawRelSet.count(pair.second) > 0)
		{
			continue;   // raw 존재 → 정상 처리됨
		}
		if (QuarantineOrphanMeta(pair.first, pair.second))
		{
			report.OrphanQuarantined++;
		}
	}

	m_assetDb.Save(GetAssetDbPath());
	m_assetDbDirty = false;
	return report;
}

bool CProjectManager::QuarantineOrphanMeta(const File::Path& metaAbsolutePath, const std::string& assetRelativePath)
{
	std::error_code ec;
	if (false == std::filesystem::exists(metaAbsolutePath, ec))
	{
		return false;
	}

	const std::string metaRel = CAssetPath::MakeMetaPath(assetRelativePath.c_str());
	const std::filesystem::path quarantineRoot =
		std::filesystem::path(m_info.RootPath) / "Intermediate" / "AssetQuarantine";
	const std::filesystem::path dest = quarantineRoot / metaRel;

	std::filesystem::create_directories(dest.parent_path(), ec);
	ec.clear();
	if (std::filesystem::exists(dest, ec)) { std::filesystem::remove(dest, ec); }
	ec.clear();

	std::filesystem::rename(metaAbsolutePath, dest, ec);
	if (ec)
	{
		// 다른 볼륨 등 rename 실패 → copy + remove 폴백.
		ec.clear();
		std::filesystem::copy_file(metaAbsolutePath, dest, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) { ec.clear(); return false; }
		std::filesystem::remove(metaAbsolutePath, ec);
		ec.clear();
	}

	CSystemLog::Warning("[AssetReconcile] orphan meta quarantined: " + metaRel);
	return true;
}

void CProjectManager::EnsureRecoveredSidecarMeta(const File::Path& absolutePath, const std::string& relativePath, EAssetType type, const char* importerName, const File::Path& metaPath)
{
	if (false == m_assetManager.IsValid() || metaPath.empty())
	{
		return;
	}

	std::uint64_t hash = 0, size = 0;
	std::int64_t  mtime = 0;
	const AssetDbEntry* rec = nullptr;
	if (CAssetDatabase::HashFile(absolutePath, hash, size, mtime))
	{
		rec = m_assetDb.FindByHash(hash);
	}
	if (nullptr == rec)
	{
		rec = m_assetDb.FindByPath(relativePath);
	}
	if (nullptr == rec || rec->Guid.IsNull())
	{
		return;   // 복구할 GUID 없음 — ImportAsset 이 새 GUID 발급.
	}
	// 그 GUID 가 현재 (다른 경로로) 살아있는 자산이면 건드리지 않는다 — 이동은 rename 로직 담당.
	if (nullptr != m_assetManager->GetRegistry().FindAsset(rec->Guid))
	{
		return;
	}

	AssetMetaData meta;
	meta.Guid              = rec->Guid;
	meta.Type              = (EAssetType::Unknown != rec->Type) ? rec->Type : type;
	meta.Version           = rec->Version ? rec->Version : 1u;
	meta.DisplayName       = rec->DisplayName.empty() ? CAssetPath::GetDisplayNameFromPath(relativePath.c_str()) : rec->DisplayName;
	meta.Importer          = rec->Importer.empty() ? std::string(importerName) : rec->Importer;
	meta.ImportOptionsYaml = rec->ImportOptionsYaml;
	meta.Path              = File::Path(relativePath);
	meta.MetaPath          = File::Path(CAssetPath::MakeMetaPath(relativePath.c_str()));
	CAssetMetaFile::Save(metaPath, meta);
	CSystemLog::Info("[AssetReconcile] recovered GUID for restored asset: " + relativePath);
}

void CProjectManager::UpdateAssetDbEntry(const AssetMetaData& metaData, const File::Path& absolutePath, const std::string& relativePath)
{
	if (metaData.Guid.IsNull())
	{
		return;
	}

	AssetDbEntry e;
	e.Guid         = metaData.Guid;
	e.RelativePath = relativePath;

	std::uint64_t hash = 0, size = 0;
	std::int64_t  mtime = 0;
	if (CAssetDatabase::HashFile(absolutePath, hash, size, mtime))
	{
		e.ContentHash  = hash;
		e.FileSize     = size;
		e.ModifiedTime = mtime;
	}
	e.Type              = metaData.Type;
	e.Version           = metaData.Version ? metaData.Version : 1u;
	e.DisplayName       = metaData.DisplayName;
	e.Importer          = metaData.Importer;
	e.ImportOptionsYaml = metaData.ImportOptionsYaml;
	m_assetDb.Upsert(e);
	m_assetDbDirty = true;
}

void CProjectManager::ProcessAssetEvents(const std::vector<FileWatchEvent>& events)
{
	// rename(이동)으로 in-place 처리된 자산 경로 — 아래 일반 이벤트 루프에서 건너뛴다.
	std::unordered_set<std::string> handledPaths;

	// ── 1) 이동/이름변경 먼저 처리 ───────────────────────────────────────────
	// Created(newAsset) 마다 매칭되는 Deleted(oldAsset) 를 찾아 레지스트리 경로만
	// 갱신한다(언로드 X). 이렇게 하면 그 GUID 로 로드돼 있던 에셋(스프라이트 텍스처
	// 등)이 파괴되지 않아 씬의 라이브 참조가 깨지지 않는다.
	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Created != event.Type)
		{
			continue;
		}
		// 무시 패턴 매칭(임시 파일 등)은 rename 매칭 자체에서 제외 — 임시 파일이 잠시
		// REMOVED/CREATED 로 나타나도 자산 rename 으로 잘못 짝지어지지 않게 한다.
		if (IsAssetPathIgnored(event.Path))
		{
			continue;
		}
		if (event.Path.empty() || CAssetPath::IsMetaPath(event.Path.generic_string().c_str()))
		{
			continue;
		}

		File::Path oldAssetPath;
		if (TryHandleAssetRename(event.Path, events, oldAssetPath))
		{
			handledPaths.insert(event.Path.generic_string());
			handledPaths.insert(oldAssetPath.generic_string());
		}
		else
		{
			// 레지스트리에 없던 자산(신규/미임포트)의 rename 은 메타만 동반 이동한다.
			// 이후 일반 루프의 ImportOrReloadAsset 이 옮겨진 메타로 같은 GUID 를 복원한다.
			TrySyncRenamedAssetMeta(event.Path, events);
		}
	}

	// ── 2) 나머지 이벤트 처리 (rename 으로 소비된 자산 경로는 건너뜀) ─────────
	for (const FileWatchEvent& event : events)
	{
		if (handledPaths.count(event.Path.generic_string()) > 0)
		{
			continue;
		}
		ProcessAssetEvent(event);
	}

	// 이번 배치에서 복구 캐시가 바뀌었으면 디스크에 반영한다(다음 로드 복구 힌트).
	if (m_assetDbDirty)
	{
		m_assetDb.Save(GetAssetDbPath());
		m_assetDbDirty = false;
	}
}

bool CProjectManager::TryHandleAssetRename(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events, File::Path& outOldAssetPath)
{
	if (false == m_assetManager.IsValid())
	{
		return false;
	}
	if (createdAssetPath.empty() || CAssetPath::IsMetaPath(createdAssetPath.generic_string().c_str()))
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(createdAssetPath, errorCode) || false == std::filesystem::is_regular_file(createdAssetPath, errorCode))
	{
		return false;
	}

	std::string newRelative;
	if (false == MakeAssetRelativePath(createdAssetPath, newRelative))
	{
		return false;
	}

	// 이동된 자산을 GUID 로 정확히 짝짓는다. 확장자만으로 매칭하면 같은 배치에서
	// 같은 확장자 파일을 여러 개 옮길 때(예: test.png + test2.png) 엉뚱하게 교차
	// 매칭되어 메타/레지스트리가 뒤바뀐다.
	// (a) 에셋브라우저 이동: .Jmeta 가 새 위치로 함께 옮겨지므로 그 GUID 가 정답.
	AssetGuid newSidecarGuid = INVALID_ASSET_GUID;
	{
		File::Path newMetaPath;
		AssetMetaData newMeta;
		if (TryGetMetaPathForAsset(createdAssetPath, newMetaPath)
			&& std::filesystem::exists(newMetaPath, errorCode)
			&& CAssetMetaFile::Load(newMetaPath, newMeta))
		{
			newSidecarGuid = newMeta.Guid;
		}
		errorCode.clear();
	}

	const std::string newFileName = createdAssetPath.filename().generic_string();
	const IAssetRegistry& registry = m_assetManager->GetRegistry();

	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Deleted != event.Type || event.Path.empty() || CAssetPath::IsMetaPath(event.Path.generic_string().c_str()))
		{
			continue;
		}
		// 무시 패턴(임시 파일 등) 인 삭제는 rename 후보에서 제외.
		if (IsAssetPathIgnored(event.Path))
		{
			continue;
		}
		// 삭제 경로가 아직 디스크에 있으면 진짜 이동이 아니다(별개의 생성/삭제).
		errorCode.clear();
		if (std::filesystem::exists(event.Path, errorCode))
		{
			continue;
		}

		std::string oldRelative;
		if (false == MakeAssetRelativePath(event.Path, oldRelative))
		{
			continue;
		}

		// 원본이 레지스트리에 등록돼 있어야 GUID 보존 이동으로 처리한다.
		const AssetMetaData* oldReg = registry.FindAssetByPath(File::Path(oldRelative));
		if (nullptr == oldReg)
		{
			continue;
		}
		const AssetGuid oldGuid = oldReg->Guid;

		bool isMatch = false;
		if (false == newSidecarGuid.IsNull())
		{
			// (a) 새 위치 .Jmeta 의 GUID 가 이 원본의 GUID 와 일치해야만 같은 자산.
			isMatch = (newSidecarGuid == oldGuid);
		}
		else if (event.Path.filename().generic_string() == newFileName)
		{
			// (b) 탐색기 이동(새 메타 없음): 같은 파일명 + 뒤에 남은 .Jmeta 의 GUID 가
			//     원본 GUID 와 일치하면 확정. 그리고 남은 메타를 새 위치로 옮겨 GUID 보존.
			File::Path oldMetaPath;
			AssetMetaData oldMeta;
			if (TryGetMetaPathForAsset(event.Path, oldMetaPath)
				&& std::filesystem::exists(oldMetaPath, errorCode)
				&& CAssetMetaFile::Load(oldMetaPath, oldMeta)
				&& oldMeta.Guid == oldGuid)
			{
				isMatch = true;
				MoveMetaForRenamedAsset(event.Path, createdAssetPath);
			}
			errorCode.clear();
		}

		if (false == isMatch)
		{
			continue;
		}

		// 레지스트리 경로만 in-place 교체 — 로드된 데이터/GUID 유지.
		if (m_assetManager->MoveAssetPath(File::Path(oldRelative), File::Path(newRelative)))
		{
			// 복구 캐시도 새 경로로 갱신(같은 GUID).
			if (const AssetMetaData* moved = m_assetManager->GetRegistry().FindAsset(oldGuid))
			{
				UpdateAssetDbEntry(*moved, createdAssetPath, newRelative);
			}
			outOldAssetPath = event.Path;
			MarkAssetDatabaseChanged();
			return true;
		}
		return false;
	}

	return false;
}

void CProjectManager::ProcessAssetEvent(const FileWatchEvent& event)
{
	if (false == m_assetManager.IsValid())
	{
		return;
	}

	// .Jmeta 의 Created/Modified 는 자기-반향(에디터가 직접 쓴 .Jmeta 를 워처가 다시 잡는 케이스).
	// 자산 옵션은 IAsset::ApplyImportOptions 경로로 메모리에 in-place 갱신되므로 워처 처리 불필요.
	// 외부 .Jmeta 변경(git pull 등) 동기화는 현재 지원 범위 밖 — tasks/asset-system-followups.md P9 참조.
	// 단 Deleted .Jmeta 는 아래 분기에서 별도로 처리 (raw 살아있으면 메타 재생성).
	if (EFileWatchEventType::Deleted != event.Type
		&& false == event.Path.empty()
		&& CAssetPath::IsMetaPath(event.Path.generic_string().c_str()))
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

bool CProjectManager::ImportOrReloadAsset(const File::Path& absolutePath, bool loadData)
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

	const char* importerName = ImporterNameForType(type);

	// 메타가 없으면 먼저 복구 캐시(DB)로 같은 GUID 의 사이드카 메타를 써 둔다.
	// (외부에서 자산을 복사로 되살리거나 메타만 지운 경우 참조가 살아남도록 —
	//  그래야 이어지는 ImportAsset 이 새 GUID 를 발급하지 않고 그 메타를 읽는다.)
	File::Path metaPath;
	if (TryGetMetaPathForAsset(absolutePath, metaPath))
	{
		std::error_code metaEc;
		if (false == std::filesystem::exists(metaPath, metaEc))
		{
			EnsureRecoveredSidecarMeta(absolutePath, relativePath, type, importerName, metaPath);
		}
	}

	AssetImportDesc importDesc;
	importDesc.Type = type;
	importDesc.Path = File::Path(relativePath);
	importDesc.Importer = importerName;
	AssetMetaData metaData;
	if (m_assetManager->ImportAsset(importDesc, &metaData))
	{
		// loadData=false 면 레지스트리 등록만 하고 데이터(텍스처 등)는 메모리에 올리지 않는다.
		if (loadData)
		{
			m_assetManager->ReloadAsset(metaData.Guid);
		}
		// 복구 캐시 갱신 — 경로/해시/메타 필드를 최신으로.
		UpdateAssetDbEntry(metaData, absolutePath, relativePath);
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

	// 사용자 설정의 무시 패턴(예: *.tmp, ~$*, *.swp)에 매칭되면 자산 import 시도하지 않는다.
	if (IsAssetPathIgnored(absolutePath))
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
	if (extension == ".wav" || extension == ".mp3" || extension == ".flac" || extension == ".ogg")
	{
		return EAssetType::Audio;
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
		// 무시 패턴 매칭된 삭제는 rename 매칭 후보에서 제외.
		if (IsAssetPathIgnored(event.Path))
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
	// 런타임(스프라이트 폴백)도 즉시 반영 — ProjectSettings Apply 등에서 호출되면 다음 프레임부터 렌더 반영.
	Engine.PixelsPerUnit = m_info.PixelsPerUnit;
}

const std::vector<std::string>& CProjectManager::GetAssetWatchIgnorePatterns() const
{
	return m_info.AssetWatchIgnorePatterns;
}

void CProjectManager::SetAssetWatchIgnorePatterns(std::vector<std::string> patterns)
{
	m_info.AssetWatchIgnorePatterns = std::move(patterns);
}

bool CProjectManager::IsAssetPathIgnored(const File::Path& absoluteOrRelativePath) const
{
	if (absoluteOrRelativePath.empty()) return false;
	if (m_info.AssetWatchIgnorePatterns.empty()) return false;

	const std::string fileName = absoluteOrRelativePath.filename().generic_string();

	// 상대경로 산출 — AssetPath 기준. 실패 시 절대경로의 generic 문자열을 사용 (그 경우 슬래시 패턴은 매칭 약함).
	std::string relativePath;
	if (false == m_info.AssetPath.empty())
	{
		std::error_code ec;
		std::filesystem::path rel = std::filesystem::relative(absoluteOrRelativePath, m_info.AssetPath, ec);
		if (!ec) relativePath = rel.generic_string();
	}
	if (relativePath.empty()) relativePath = absoluteOrRelativePath.generic_string();

	return MatchAnyPattern(m_info.AssetWatchIgnorePatterns, fileName, relativePath);
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

const ProjectBuildSettings& CProjectManager::GetBuildSettings() const
{
	return m_info.BuildSettings;
}

void CProjectManager::SetBuildSettings(const ProjectBuildSettings& settings)
{
	m_info.BuildSettings = settings;
	NormalizeBuildSettings(m_info.BuildSettings, m_info.ProjectFilePath);
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

bool CProjectManager::RegenerateScriptProject() const
{
	if (false == m_isProjectLoaded)
	{
		return false;
	}
	CGameScriptProjectGenerator generator;
	return generator.EnsureProject(m_info);
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
	// 솔루션(.sln) 우선, 없으면 .vcxproj 로 폴백.  매 호출마다 OpenFile 을 실행한다 —
	// VS 는 이미 같은 솔루션을 열어둔 상태면 새 인스턴스를 띄우지 않고 기존 윈도우를
	// 활성화하며, 닫아둔 상태면 다시 연다.  영구 캐시로 path 를 기억해서 한 번 열린
	// 뒤 영원히 skip 하면 사용자가 VS 를 종료한 다음에 재오픈을 못 한다.
	const File::Path slnPath = FindScriptSolutionPath();
	File::Path idePath = slnPath.empty() ? FindScriptVcxprojPath() : slnPath;
	if (idePath.empty() && filePath.empty())
	{
		return;
	}

	if (false == idePath.empty())
	{
		File::OpenFile(idePath);
	}

	// 사용자가 특정 스크립트(.cpp/.h)를 더블클릭한 경우 그 파일도 함께 열어 활성 탭으로.
	// 이미 솔루션이 열려 있으면 OS 가 같은 VS 인스턴스에서 새 탭으로 띄운다.
	if (false == filePath.empty())
	{
		std::error_code ec;
		if (std::filesystem::exists(filePath, ec))
		{
			File::OpenFile(filePath);
		}
	}
}

bool CProjectManager::SaveProject(std::string* outError) const
{
	if (false == m_isProjectLoaded || m_info.ProjectFilePath.empty())
	{
		if (outError)
		{
			*outError = "Project is not loaded.";
		}
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
	out << YAML::Key << PROJECT_KEY_BUILD << YAML::Value;
	out << YAML::BeginMap;
	out << YAML::Key << PROJECT_KEY_BUILD_PRODUCT_NAME << YAML::Value << m_info.BuildSettings.ProductName;
	out << YAML::Key << PROJECT_KEY_BUILD_TARGET_PLATFORM << YAML::Value << ToString(m_info.BuildSettings.TargetPlatform);
	out << YAML::Key << PROJECT_KEY_BUILD_CONFIGURATION << YAML::Value << ToString(m_info.BuildSettings.BuildConfiguration);
	out << YAML::Key << PROJECT_KEY_BUILD_OUTPUT_DIR << YAML::Value << m_info.BuildSettings.OutputDirectory;
	out << YAML::Key << PROJECT_KEY_BUILD_STARTUP_SCENE << YAML::Value << m_info.BuildSettings.StartupScene;
	out << YAML::Key << PROJECT_KEY_BUILD_SCENES << YAML::Value;
	out << YAML::BeginSeq;
	for (const std::string& scene : m_info.BuildSettings.BuildScenes)
	{
		out << scene;
	}
	out << YAML::EndSeq;
	out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_MODE << YAML::Value << ToString(m_info.BuildSettings.ScriptMode);
	out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_PROJECT << YAML::Value << m_info.BuildSettings.ScriptProjectPath;
	out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_CONFIG << YAML::Value << ToString(m_info.BuildSettings.ScriptBuildConfiguration);
	out << YAML::Key << PROJECT_KEY_BUILD_SCRIPT_OUTPUT << YAML::Value << m_info.BuildSettings.ScriptOutputLibraryPath;
	out << YAML::Key << PROJECT_KEY_BUILD_WINDOWS_ICON_GUID << YAML::Value << m_info.BuildSettings.WindowsIconGuid.generic_string();
	out << YAML::EndMap;
	if (false == m_info.ScriptDllPath.empty())
	{
		out << YAML::Key << PROJECT_KEY_SCRIPT_DLL_PATH << YAML::Value << m_info.ScriptDllPath;
	}
	if (false == m_info.LastOpenedScenePath.empty())
	{
		out << YAML::Key << PROJECT_KEY_LAST_SCENE_PATH << YAML::Value << m_info.LastOpenedScenePath;
	}
	out << YAML::Key << PROJECT_KEY_WATCH_IGNORE << YAML::Value;
	out << YAML::BeginSeq;
	for (const std::string& pattern : m_info.AssetWatchIgnorePatterns)
	{
		out << pattern;
	}
	out << YAML::EndSeq;
	if (false == m_info.ImGuiIniSettings.empty())
	{
		out << YAML::Key << PROJECT_KEY_IMGUI_INI << YAML::Value << YAML::Literal << m_info.ImGuiIniSettings;
	}
	out << YAML::EndMap;

	std::ofstream file(m_info.ProjectFilePath, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		if (outError)
		{
			*outError = "Failed to open project file for writing: " + m_info.ProjectFilePath.generic_string();
		}
		return false;
	}
	file << out.c_str();
	if (false == static_cast<bool>(file))
	{
		if (outError)
		{
			*outError = "Failed to write project file: " + m_info.ProjectFilePath.generic_string();
		}
		return false;
	}
	if (outError)
	{
		outError->clear();
	}
	return true;
}
