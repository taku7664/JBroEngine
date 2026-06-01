#pragma once

#include "Engine/Editor/Project/ProjectTypes.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

class CProjectManager;

enum class EGameBuildState
{
	Idle,
	Running,
	Succeeded,
	Failed
};

enum class EGameBuildTaskState
{
	Pending,
	Running,
	Completed,
	Failed
};

struct GameBuildTaskProgress
{
	std::string Name;
	std::string Description;
	EGameBuildTaskState State = EGameBuildTaskState::Pending;
};

struct GameBuildSnapshot
{
	EGameBuildState State = EGameBuildState::Idle;
	std::uint32_t CompletedCount = 0;
	std::uint32_t TotalCount = 0;
	float Progress01 = 0.0f;
	std::vector<GameBuildTaskProgress> Tasks;
	std::string Message;
	File::Path PackageDirectory;
	File::Path LogPath;
	bool OutputOpened = false;
};

class CGameBuildManager final
{
public:
	CGameBuildManager() = default;
	~CGameBuildManager();

	CGameBuildManager(const CGameBuildManager&) = delete;
	CGameBuildManager& operator=(const CGameBuildManager&) = delete;

	bool StartBuild(SafePtr<CProjectManager> projectManager);
	bool IsRunning() const;
	GameBuildSnapshot GetSnapshot() const;
	void MarkOutputOpened();
	void ClearResult();

private:
	struct BuildDesc
	{
		File::Path RepoRoot;
		File::Path ProjectFilePath;
		File::Path ProjectRoot;
		File::Path ContentPath;
		File::Path AssetPath;
		File::Path ScriptProjectPath;
		std::string ProductName;
		EBuildTargetPlatform TargetPlatform = EBuildTargetPlatform::Windows;
		EBuildConfiguration BuildConfiguration = EBuildConfiguration::Release;
		std::uint32_t ResolutionWidth = 1920;
		std::uint32_t ResolutionHeight = 1080;
		float PixelsPerUnit = 100.0f;
		std::string OutputDirectory;
		std::string StartupScene;
		std::string StartupSceneGuid;
		std::vector<std::string> BuildScenes;
		AssetGuid WindowsIconGuid = INVALID_ASSET_GUID;
		File::Path OutputRoot;
		File::Path PackageDirectory;
		File::Path LogPath;
	};

	void ResetForStart(BuildDesc desc, std::vector<GameBuildTaskProgress> tasks);
	void WorkerMain(BuildDesc desc);
	void JoinWorker();

	void SetTaskState(std::size_t index, EGameBuildTaskState state);
	void SetFinished(EGameBuildState state, std::string message);
	void AppendLog(const BuildDesc& desc, const std::string& text) const;

	bool ValidateScenes(const BuildDesc& desc, std::string& outError) const;
	bool BuildEngineGame(const BuildDesc& desc, std::string& outError) const;
	bool BuildScriptModule(const BuildDesc& desc, File::Path& outScriptDll, std::string& outError) const;
	bool StagePackage(const BuildDesc& desc, const File::Path& scriptDll, std::string& outError) const;
	bool VerifyPackage(const BuildDesc& desc, bool requiresScriptDll, std::string& outError) const;
	bool ApplyWindowsIconToExecutable(const BuildDesc& desc, const File::Path& executablePath, std::string& outError) const;

	File::Path FindMSBuildPath() const;
	bool RunCommandToLog(const std::wstring& command, const File::Path& logPath, std::string& outError) const;
	File::Path ResolveOutputRoot(const File::Path& projectRoot, const std::string& outputDirectory) const;
	File::Path FindScriptDll(const BuildDesc& desc) const;

private:
	mutable std::mutex m_mutex;
	std::thread m_worker;
	EGameBuildState m_state = EGameBuildState::Idle;
	std::vector<GameBuildTaskProgress> m_tasks;
	std::uint32_t m_completedCount = 0;
	std::string m_message;
	File::Path m_packageDirectory;
	File::Path m_logPath;
	bool m_outputOpened = false;
	std::atomic_bool m_running = false;
};

#endif
