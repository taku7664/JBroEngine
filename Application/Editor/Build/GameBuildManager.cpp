#include "pch.h"
#include "GameBuildManager.h"

#include "Editor/Editor.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Build/BuildManifest.h"
#include "Engine/Core/Core.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/String/StringUtillity.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
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

	const char* ToEngineConfiguration(EBuildConfiguration configuration)
	{
		return EBuildConfiguration::Debug == configuration ? "Debug_Game" : "Release_Game";
	}

	std::string ToUtf8PathString(const File::Path& path)
	{
		const auto text = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
	}

	std::wstring Quote(const std::wstring& value)
	{
		return L"\"" + value + L"\"";
	}

	std::wstring GetEnvironmentVariableValue(const wchar_t* name)
	{
		wchar_t* value = nullptr;
		std::size_t valueLength = 0;
		if (0 != _wdupenv_s(&value, &valueLength, name) || nullptr == value)
		{
			return {};
		}

		std::wstring result(value);
		std::free(value);
		return result;
	}

	std::string SanitizeFileName(std::string value)
	{
		static constexpr char invalid[] = "<>:\"/\\|?*";
		for (char& ch : value)
		{
			if (static_cast<unsigned char>(ch) < 32 || std::strchr(invalid, ch) != nullptr)
			{
				ch = '_';
			}
		}
		if (value.empty())
		{
			return "Game";
		}
		return value;
	}

	File::Path FindAncestorWithSolution(File::Path start)
	{
		std::error_code ec;
		if (start.empty())
		{
			return File::NULL_PATH;
		}
		start = std::filesystem::absolute(start, ec);
		if (ec)
		{
			return File::NULL_PATH;
		}
		if (std::filesystem::is_regular_file(start, ec))
		{
			start = start.parent_path();
		}

		for (File::Path cursor = start; false == cursor.empty(); cursor = cursor.parent_path())
		{
			if (std::filesystem::exists(cursor / "JBroEngine.sln", ec))
			{
				return cursor;
			}
			if (cursor == cursor.parent_path())
			{
				break;
			}
		}
		return File::NULL_PATH;
	}

	File::Path ResolveRepoRoot(File::Path preferred)
	{
		if (File::Path result = FindAncestorWithSolution(preferred); false == result.empty())
		{
			return result;
		}
		if (File::Path result = FindAncestorWithSolution(std::filesystem::current_path()); false == result.empty())
		{
			return result;
		}

		wchar_t modulePath[MAX_PATH] = {};
		if (0 != GetModuleFileNameW(nullptr, modulePath, MAX_PATH))
		{
			if (File::Path result = FindAncestorWithSolution(modulePath); false == result.empty())
			{
				return result;
			}
		}
		return preferred;
	}

	bool IsInside(const File::Path& root, const File::Path& target)
	{
		std::error_code ec;
		const File::Path absRoot = std::filesystem::weakly_canonical(root, ec);
		if (ec) return false;
		const File::Path absTarget = std::filesystem::weakly_canonical(target, ec);
		if (ec) return false;

		auto rootIt = absRoot.begin();
		auto targetIt = absTarget.begin();
		for (; rootIt != absRoot.end(); ++rootIt, ++targetIt)
		{
			if (targetIt == absTarget.end() || *rootIt != *targetIt)
			{
				return false;
			}
		}
		return true;
	}

	bool RemoveDirectoryInside(const File::Path& root, const File::Path& target, std::string& outError)
	{
		if (false == std::filesystem::exists(target))
		{
			return true;
		}
		if (false == IsInside(root, target))
		{
			outError = "Refusing to delete outside output root: " + ToUtf8PathString(target);
			return false;
		}

		std::error_code ec;
		std::filesystem::remove_all(target, ec);
		if (ec)
		{
			outError = ec.message();
			return false;
		}
		return true;
	}

	bool SaveCurrentEditorStateForBuild(SafePtr<CProjectManager> pm, std::string& outError)
	{
		if (false == pm.IsValid() || false == pm->IsProjectLoaded())
		{
			outError = "Project is not loaded.";
			return false;
		}

		if (Core::SceneManager.IsValid())
		{
			SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
			if (scene.IsValid() && false == Editor::GetActiveScenePath().empty())
			{
				CSceneSerializer serializer;
				if (ESceneSerializeResult::Success == serializer.SaveToFile(*scene, Editor::GetActiveScenePath()))
				{
					Editor::CommandManager.MarkSaved();
				}
				else
				{
					outError = "Failed to save active scene before build.";
					return false;
				}
			}
		}

		if (Editor::SceneView)
		{
			pm->SetSceneViewCamera(
				Editor::SceneView->GetEditorCameraPos().x,
				Editor::SceneView->GetEditorCameraPos().y,
				Editor::SceneView->GetEditorCameraSize());
		}

		const File::Path& activeScenePath = Editor::GetActiveScenePath();
		if (false == activeScenePath.empty() && false == pm->GetAssetPath().empty())
		{
			std::error_code ec;
			File::Path relativePath = std::filesystem::relative(activeScenePath, pm->GetAssetPath(), ec);
			if (false == static_cast<bool>(ec) && false == relativePath.empty())
			{
				pm->SetLastOpenedScenePath(relativePath.generic_string());
			}
		}

		return pm->SaveProject(&outError);
	}

	AssetGuid FindAssetGuidByPath(const File::Path& assetPath)
	{
		SafePtr<IAssetManager> assetManager = Core::AssetManager;
		if (false == assetManager.IsValid())
		{
			return INVALID_ASSET_GUID;
		}

		const AssetMetaData* metaData = assetManager->GetRegistry().FindAssetByPath(assetPath);
		return metaData ? metaData->Guid : INVALID_ASSET_GUID;
	}
}

CGameBuildManager::~CGameBuildManager()
{
	JoinWorker();
}

bool CGameBuildManager::StartBuild(SafePtr<CProjectManager> projectManager)
{
	if (IsRunning() || false == projectManager.IsValid() || false == projectManager->IsProjectLoaded())
	{
		return false;
	}

	std::string saveError;
	if (false == SaveCurrentEditorStateForBuild(projectManager, saveError))
	{
		std::lock_guard lock(m_mutex);
		m_state = EGameBuildState::Failed;
		m_message = false == saveError.empty() ? saveError : "Failed to save project before build.";
		m_tasks.clear();
		m_completedCount = 0;
		m_packageDirectory.clear();
		m_logPath.clear();
		return false;
	}

	ProjectBuildSettings settings = projectManager->GetBuildSettings();
	if (settings.BuildScenes.empty() && false == settings.StartupScene.empty())
	{
		settings.BuildScenes.push_back(settings.StartupScene);
	}
	if (EBuildTargetPlatform::Windows == settings.TargetPlatform)
	{
		settings.ScriptMode = EBuildScriptMode::DynamicLibrary;
		settings.ScriptProjectPath = std::string("Contents") + "/GameScript.vcxproj";
		settings.ScriptOutputLibraryPath = "GameScript.dll";
	}
	else
	{
		settings.ScriptMode = EBuildScriptMode::Static;
		settings.ScriptProjectPath.clear();
		settings.ScriptOutputLibraryPath.clear();
	}
	settings.ScriptBuildConfiguration = EBuildConfiguration::Debug == settings.BuildConfiguration
		? EScriptBuildConfiguration::Debug
		: EScriptBuildConfiguration::Release;
	if (settings.TargetPlatform != EBuildTargetPlatform::Windows)
	{
		std::lock_guard lock(m_mutex);
		m_state = EGameBuildState::Failed;
		m_message = "Only Windows package build is implemented in the editor right now.";
		m_tasks.clear();
		m_completedCount = 0;
		m_packageDirectory.clear();
		m_logPath.clear();
		return false;
	}

	if (settings.StartupScene.empty())
	{
		std::lock_guard lock(m_mutex);
		m_state = EGameBuildState::Failed;
		m_message = "Startup scene is not configured.";
		m_tasks.clear();
		m_completedCount = 0;
		m_packageDirectory.clear();
		m_logPath.clear();
		return false;
	}

	if (false == projectManager->RegenerateScriptProject())
	{
		std::lock_guard lock(m_mutex);
		m_state = EGameBuildState::Failed;
		m_message = "Failed to regenerate script project.";
		m_tasks.clear();
		m_completedCount = 0;
		m_packageDirectory.clear();
		m_logPath.clear();
		return false;
	}
	if (Core::AssetManager)
	{
		Core::AssetManager->RefreshAssetRegistry();
	}

	BuildDesc desc;
	desc.RepoRoot = ResolveRepoRoot(projectManager->GetOriginPath());
	desc.ProjectFilePath = projectManager->GetProjectFilePath();
	desc.ProjectRoot = projectManager->GetRootPath();
	desc.ContentPath = projectManager->GetContentPath();
	desc.AssetPath = projectManager->GetAssetPath();
	desc.ScriptProjectPath = projectManager->FindScriptVcxprojPath();
	desc.ProductName = SanitizeFileName(settings.ProductName);
	desc.TargetPlatform = settings.TargetPlatform;
	desc.BuildConfiguration = settings.BuildConfiguration;
	desc.ResolutionWidth = projectManager->GetResolutionWidth();
	desc.ResolutionHeight = projectManager->GetResolutionHeight();
	desc.OutputDirectory = settings.OutputDirectory;
	desc.StartupScene = settings.StartupScene;
	desc.StartupSceneGuid = FindAssetGuidByPath(File::Path(Utillity::U8ToWString(settings.StartupScene))).generic_string();
	desc.BuildScenes = settings.BuildScenes;
	desc.OutputRoot = ResolveOutputRoot(desc.ProjectRoot, settings.OutputDirectory);
	desc.PackageDirectory = desc.OutputRoot / (desc.ProductName + "-" + ToString(desc.TargetPlatform) + "-" + ToString(desc.BuildConfiguration));
	desc.LogPath = desc.ProjectRoot / "Build" / "Logs" / "EditorGameBuild.log";

	std::vector<GameBuildTaskProgress> tasks = {
		{ "Validate", Loc::Text("build.progress.validate"), EGameBuildTaskState::Pending },
		{ "BuildEngine", Loc::Text("build.progress.engine"), EGameBuildTaskState::Pending },
		{ "BuildScripts", Loc::Text("build.progress.scripts"), EGameBuildTaskState::Pending },
		{ "Package", Loc::Text("build.progress.package"), EGameBuildTaskState::Pending },
		{ "Verify", Loc::Text("build.progress.verify"), EGameBuildTaskState::Pending },
	};

	JoinWorker();
	ResetForStart(desc, std::move(tasks));
	m_worker = std::thread([this, desc]() { WorkerMain(desc); });
	return true;
}

bool CGameBuildManager::IsRunning() const
{
	return m_running.load();
}

GameBuildSnapshot CGameBuildManager::GetSnapshot() const
{
	std::lock_guard lock(m_mutex);
	GameBuildSnapshot snapshot;
	snapshot.State = m_state;
	snapshot.CompletedCount = m_completedCount;
	snapshot.TotalCount = static_cast<std::uint32_t>(m_tasks.size());
	snapshot.Progress01 = m_tasks.empty() ? 0.0f : static_cast<float>(m_completedCount) / static_cast<float>(m_tasks.size());
	snapshot.Tasks = m_tasks;
	snapshot.Message = m_message;
	snapshot.PackageDirectory = m_packageDirectory;
	snapshot.LogPath = m_logPath;
	snapshot.OutputOpened = m_outputOpened;
	return snapshot;
}

void CGameBuildManager::MarkOutputOpened()
{
	std::lock_guard lock(m_mutex);
	m_outputOpened = true;
}

void CGameBuildManager::ClearResult()
{
	if (IsRunning())
	{
		return;
	}

	std::lock_guard lock(m_mutex);
	m_state = EGameBuildState::Idle;
	m_tasks.clear();
	m_completedCount = 0;
	m_message.clear();
	m_packageDirectory.clear();
	m_logPath.clear();
	m_outputOpened = false;
}

void CGameBuildManager::ResetForStart(BuildDesc desc, std::vector<GameBuildTaskProgress> tasks)
{
	std::lock_guard lock(m_mutex);
	m_state = EGameBuildState::Running;
	m_tasks = std::move(tasks);
	m_completedCount = 0;
	m_message.clear();
	m_packageDirectory = desc.PackageDirectory;
	m_logPath = desc.LogPath;
	m_outputOpened = false;
	m_running.store(true);
}

void CGameBuildManager::WorkerMain(BuildDesc desc)
{
	std::string error;
	File::Path scriptDll;

	auto runStep = [&](std::size_t index, auto&& func) -> bool
	{
		SetTaskState(index, EGameBuildTaskState::Running);
		if (false == func())
		{
			SetTaskState(index, EGameBuildTaskState::Failed);
			SetFinished(EGameBuildState::Failed, error);
			return false;
		}
		SetTaskState(index, EGameBuildTaskState::Completed);
		return true;
	};

	if (false == runStep(0, [&]() { return ValidateScenes(desc, error); })) return;
	if (false == runStep(1, [&]() { return BuildEngineGame(desc, error); })) return;
	if (false == runStep(2, [&]() { return BuildScriptModule(desc, scriptDll, error); })) return;
	if (false == runStep(3, [&]() { return StagePackage(desc, scriptDll, error); })) return;
	if (false == runStep(4, [&]() { return VerifyPackage(desc, true, error); })) return;

	SetFinished(EGameBuildState::Succeeded, "Build completed.");
}

void CGameBuildManager::JoinWorker()
{
	if (m_worker.joinable())
	{
		m_worker.join();
	}
}

void CGameBuildManager::SetTaskState(std::size_t index, EGameBuildTaskState state)
{
	std::lock_guard lock(m_mutex);
	if (index >= m_tasks.size())
	{
		return;
	}
	if (state == EGameBuildTaskState::Completed && m_tasks[index].State != EGameBuildTaskState::Completed)
	{
		++m_completedCount;
	}
	m_tasks[index].State = state;
}

void CGameBuildManager::SetFinished(EGameBuildState state, std::string message)
{
	std::lock_guard lock(m_mutex);
	m_state = state;
	m_message = std::move(message);
	m_running.store(false);
}

void CGameBuildManager::AppendLog(const BuildDesc& desc, const std::string& text) const
{
	std::error_code ec;
	std::filesystem::create_directories(desc.LogPath.parent_path(), ec);
	std::ofstream out(desc.LogPath, std::ios::out | std::ios::app);
	if (out.is_open())
	{
		out << text << "\n";
	}
}

bool CGameBuildManager::ValidateScenes(const BuildDesc& desc, std::string& outError) const
{
	if (desc.StartupScene.empty())
	{
		outError = "Startup scene is empty.";
		return false;
	}
	std::vector<std::string> scenes = desc.BuildScenes;
	if (std::find(scenes.begin(), scenes.end(), desc.StartupScene) == scenes.end())
	{
		scenes.insert(scenes.begin(), desc.StartupScene);
	}
	for (const std::string& scene : scenes)
	{
		if (scene.empty()) continue;
		const File::Path scenePath = desc.AssetPath / File::Path(Utillity::U8ToWString(scene));
		std::error_code ec;
		if (false == std::filesystem::exists(scenePath, ec))
		{
			outError = "Build scene was not found: " + scene;
			return false;
		}
		if (FindAssetGuidByPath(File::Path(Utillity::U8ToWString(scene))).IsNull())
		{
			outError = "Build scene has no registered asset GUID: " + scene;
			return false;
		}
	}
	if (AssetGuid(desc.StartupSceneGuid).IsNull())
	{
		outError = "Startup scene has no registered asset GUID: " + desc.StartupScene;
		return false;
	}
	return true;
}

bool CGameBuildManager::BuildEngineGame(const BuildDesc& desc, std::string& outError) const
{
	const File::Path msbuild = FindMSBuildPath();
	const File::Path solution = desc.RepoRoot / "JBroEngine.sln";
	const std::wstring command =
		Quote(msbuild.wstring()) + L" " +
		Quote(solution.wstring()) +
		L" /m /p:Configuration=" + Utillity::U8ToWString(ToEngineConfiguration(desc.BuildConfiguration)) +
		L" /p:Platform=x64 /v:minimal /nr:false";
	return RunCommandToLog(command, desc.LogPath, outError);
}

bool CGameBuildManager::BuildScriptModule(const BuildDesc& desc, File::Path& outScriptDll, std::string& outError) const
{
	if (desc.ScriptProjectPath.empty())
	{
		outError = "Script project was not found.";
		return false;
	}

	std::string solutionDir = ToUtf8PathString(desc.ProjectRoot);
	if (false == solutionDir.empty() && solutionDir.back() != '/' && solutionDir.back() != '\\')
	{
		solutionDir += "\\";
	}

	const File::Path msbuild = FindMSBuildPath();
	const std::wstring command =
		Quote(msbuild.wstring()) + L" " +
		Quote(desc.ScriptProjectPath.wstring()) +
		L" /p:Configuration=" + Utillity::U8ToWString(ToString(desc.BuildConfiguration)) +
		L" /p:Platform=x64" +
		L" /p:SolutionDir=" + Quote(Utillity::U8ToWString(solutionDir)) +
		L" /p:CL_MPCount=1 /p:UseMultiToolTask=false /v:minimal /nr:false";
	if (false == RunCommandToLog(command, desc.LogPath, outError))
	{
		return false;
	}

	outScriptDll = FindScriptDll(desc);
	if (outScriptDll.empty())
	{
		outError = "GameScript.dll was not found after script build.";
		return false;
	}
	return true;
}

bool CGameBuildManager::StagePackage(const BuildDesc& desc, const File::Path& scriptDll, std::string& outError) const
{
	if (false == RemoveDirectoryInside(desc.OutputRoot, desc.PackageDirectory, outError))
	{
		return false;
	}

	std::error_code ec;
	const File::Path packageContent = desc.PackageDirectory / "Content";
	const File::Path packagePack = packageContent / "game_assets.jbpack";
	std::filesystem::create_directories(packageContent, ec);
	if (ec)
	{
		outError = ec.message();
		return false;
	}

	const File::Path applicationSource = desc.RepoRoot / "Build" / ToEngineConfiguration(desc.BuildConfiguration) / "Application.exe";
	const File::Path applicationDest = desc.PackageDirectory / (desc.ProductName + ".exe");
	std::filesystem::copy_file(applicationSource, applicationDest, std::filesystem::copy_options::overwrite_existing, ec);
	if (ec)
	{
		outError = "Failed to copy Application.exe: " + ec.message();
		return false;
	}

	std::filesystem::copy_file(scriptDll, desc.PackageDirectory / "GameScript.dll", std::filesystem::copy_options::overwrite_existing, ec);
	if (ec)
	{
		outError = "Failed to copy GameScript.dll: " + ec.message();
		return false;
	}

	SafePtr<IAssetManager> assetManager = Core::AssetManager;
	if (false == assetManager.IsValid())
	{
		outError = "AssetManager is not available for packaging.";
		return false;
	}
	AssetPackageBuildDesc packageDesc;
	packageDesc.OutputManifestPath = packagePack;
	packageDesc.OutputBlobPath = packagePack;
	if (false == assetManager->BuildAssetPackage(packageDesc))
	{
		outError = "Failed to build asset pack.";
		return false;
	}

	BuildManifest manifest;
	manifest.Version = 1;
	manifest.StartupScene = desc.StartupScene;
	manifest.StartupSceneGuid = desc.StartupSceneGuid;
	manifest.ResolutionWidth = static_cast<int>(desc.ResolutionWidth);
	manifest.ResolutionHeight = static_cast<int>(desc.ResolutionHeight);
	std::string manifestError;
	if (false == CBuildManifestLoader::WriteBinaryFile(packageContent / "build_manifest.jbmanifest", manifest, &manifestError))
	{
		outError = false == manifestError.empty() ? manifestError : "Failed to write build_manifest.jbmanifest.";
		return false;
	}
	return true;
}

bool CGameBuildManager::VerifyPackage(const BuildDesc& desc, bool requiresScriptDll, std::string& outError) const
{
	const File::Path exe = desc.PackageDirectory / (desc.ProductName + ".exe");
	const File::Path dll = desc.PackageDirectory / "GameScript.dll";
	const File::Path manifest = desc.PackageDirectory / "Content" / "build_manifest.jbmanifest";
	const File::Path assetPack = desc.PackageDirectory / "Content" / "game_assets.jbpack";

	if (false == std::filesystem::exists(exe))
	{
		outError = "Package executable is missing.";
		return false;
	}
	if (requiresScriptDll && false == std::filesystem::exists(dll))
	{
		outError = "GameScript.dll is missing.";
		return false;
	}
	if (false == std::filesystem::exists(manifest))
	{
		outError = "build_manifest.jbmanifest is missing.";
		return false;
	}
	if (false == std::filesystem::exists(assetPack))
	{
		outError = "Asset pack is missing.";
		return false;
	}
	if (std::filesystem::exists(desc.PackageDirectory / "Content" / "Assets"))
	{
		outError = "Loose asset folder must not exist in package.";
		return false;
	}

	const std::array<const char*, 3> forbidden = { "SDK", "Editor", "Localization" };
	for (const char* name : forbidden)
	{
		if (std::filesystem::exists(desc.PackageDirectory / name))
		{
			outError = std::string("Forbidden editor-only artifact found: ") + name;
			return false;
		}
	}
	return true;
}

File::Path CGameBuildManager::FindMSBuildPath() const
{
	std::vector<File::Path> candidates;
	const std::wstring vsInstallDir = GetEnvironmentVariableValue(L"VSINSTALLDIR");
	if (false == vsInstallDir.empty())
	{
		candidates.emplace_back(File::Path(vsInstallDir) / L"MSBuild" / L"Current" / L"Bin" / L"MSBuild.exe");
	}

	const std::wstring programFiles = GetEnvironmentVariableValue(L"ProgramFiles");
	const std::wstring programFilesX86 = GetEnvironmentVariableValue(L"ProgramFiles(x86)");
	const std::wstring roots[] = { programFiles, programFilesX86 };
	const wchar_t* editions[] = { L"Community", L"Professional", L"Enterprise", L"BuildTools" };
	for (const std::wstring& root : roots)
	{
		if (root.empty()) continue;
		for (const wchar_t* edition : editions)
		{
			candidates.emplace_back(File::Path(root) / L"Microsoft Visual Studio" / L"2022" / edition / L"MSBuild" / L"Current" / L"Bin" / L"MSBuild.exe");
		}
	}

	for (const File::Path& candidate : candidates)
	{
		if (std::filesystem::exists(candidate))
		{
			return candidate;
		}
	}
	return L"MSBuild.exe";
}

bool CGameBuildManager::RunCommandToLog(const std::wstring& command, const File::Path& logPath, std::string& outError) const
{
	std::error_code ec;
	std::filesystem::create_directories(logPath.parent_path(), ec);

	SECURITY_ATTRIBUTES securityAttributes = {};
	securityAttributes.nLength = sizeof(securityAttributes);
	securityAttributes.bInheritHandle = TRUE;

	HANDLE logHandle = CreateFileW(
		logPath.wstring().c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		&securityAttributes,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (INVALID_HANDLE_VALUE == logHandle)
	{
		outError = "Failed to open build log file.";
		return false;
	}

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESTDHANDLES;
	startupInfo.hStdOutput = logHandle;
	startupInfo.hStdError = logHandle;
	startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION processInfo = {};
	std::vector<wchar_t> commandLine(command.begin(), command.end());
	commandLine.push_back(L'\0');
	const BOOL created = CreateProcessW(
		nullptr,
		commandLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startupInfo,
		&processInfo);
	if (FALSE == created)
	{
		CloseHandle(logHandle);
		outError = "Failed to start build command.";
		return false;
	}

	WaitForSingleObject(processInfo.hProcess, INFINITE);

	DWORD processExitCode = 1;
	GetExitCodeProcess(processInfo.hProcess, &processExitCode);
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	CloseHandle(logHandle);

	if (0 != processExitCode)
	{
		outError = "Build command failed. See log: " + ToUtf8PathString(logPath);
		return false;
	}
	return true;
}

File::Path CGameBuildManager::ResolveOutputRoot(const File::Path& projectRoot, const std::string& outputDirectory) const
{
	if (outputDirectory.empty())
	{
		return projectRoot / "Dist" / "Games";
	}

	File::Path output = File::Path(Utillity::U8ToWString(outputDirectory));
	if (output.is_absolute())
	{
		return output;
	}
	return projectRoot / output;
}

File::Path CGameBuildManager::FindScriptDll(const BuildDesc& desc) const
{
	const char* configuration = ToString(desc.BuildConfiguration);
	std::vector<File::Path> candidates = {
		desc.ProjectRoot / "GameScript.dll",
		desc.ProjectRoot / "x64" / configuration / "GameScript.dll",
		desc.ScriptProjectPath.parent_path() / ".." / "x64" / configuration / "GameScript.dll",
	};

	for (const File::Path& candidate : candidates)
	{
		std::error_code ec;
		const File::Path full = std::filesystem::weakly_canonical(candidate, ec);
		const File::Path& pathToCheck = ec ? candidate : full;
		if (std::filesystem::exists(pathToCheck))
		{
			return pathToCheck;
		}
	}
	return File::NULL_PATH;
}

#endif
