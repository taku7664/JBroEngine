#include "pch.h"
#include "LiveCompileManager.h"

#include "Core/Game/IGameModule.h"
#include "Editor/LiveCompile/CompilePipeline.h"
#include "Editor/LiveCompile/Windows/WindowsDynamicLibrary.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

bool CLiveCompileManager::Initialize(const LiveCompileDesc& desc)
{
	Finalize();
	m_desc = desc;
	m_compilePipeline = MakeOwnerPtr<CCompilePipeline>();
	m_dynamicLibrary = MakeOwnerPtr<CWindowsDynamicLibrary>();
	m_sourceWatcher = MakeOwnerPtr<CWindowsFileWatcher>();
	if (!m_compilePipeline || !m_dynamicLibrary || !m_sourceWatcher)
	{
		return false;
	}

	if (false == desc.SourceDirectory.empty())
	{
		FileWatcherDesc watcherDesc;
		watcherDesc.RootPath = File::Path(desc.SourceDirectory);
		watcherDesc.Recursive = true;
		m_sourceWatcher->Watch(watcherDesc);
	}

	m_state = ELiveCompileState::Idle;
	return true;
}

void CLiveCompileManager::Finalize()
{
	DestroyCurrentModule();
	if (m_dynamicLibrary)
	{
		m_dynamicLibrary->Unload();
	}
	if (m_sourceWatcher)
	{
		m_sourceWatcher->Stop();
	}
	m_sourceWatcher.Reset();
	m_dynamicLibrary.Reset();
	m_compilePipeline.Reset();
	m_state = ELiveCompileState::Idle;
	m_isDirty = false;
}

void CLiveCompileManager::Tick()
{
	if (m_sourceWatcher)
	{
		m_sourceWatcher->Poll();
		std::vector<FileWatchEvent> events;
		if (m_sourceWatcher->TakeEvents(events))
		{
			m_isDirty = true;
			m_dirtyTime = std::chrono::steady_clock::now();
		}
	}

	if (m_isDirty)
	{
		const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - m_dirtyTime;
		if (elapsed.count() >= m_desc.DebounceSeconds)
		{
			m_isDirty = false;
			RebuildAndReload();
		}
	}
}

LiveCompileResult CLiveCompileManager::RebuildAndReload()
{
	if (!m_compilePipeline || !m_dynamicLibrary)
	{
		LiveCompileResult result;
		result.Message = "LiveCompile is not initialized.";
		return result;
	}

	m_state = ELiveCompileState::Compiling;
	LiveCompileResult result = m_compilePipeline->Compile(m_desc);
	if (false == result.Succeeded)
	{
		m_state = ELiveCompileState::Failed;
		return result;
	}

	const File::Path loadablePath = MakeLoadableLibraryPath();
	std::error_code errorCode;
	std::filesystem::create_directories(loadablePath.parent_path(), errorCode);
	std::filesystem::copy_file(m_desc.OutputLibraryPath, loadablePath, std::filesystem::copy_options::overwrite_existing, errorCode);
	if (errorCode)
	{
		result.Succeeded = false;
		result.Message = errorCode.message();
		m_state = ELiveCompileState::Failed;
		return result;
	}

	DestroyCurrentModule();
	m_dynamicLibrary->Unload();
	if (false == m_dynamicLibrary->Load(loadablePath.string().c_str()))
	{
		result.Succeeded = false;
		result.Message = "Failed to load compiled game module.";
		m_state = ELiveCompileState::Failed;
		return result;
	}

	CreateGameModuleFunc createGameModule = reinterpret_cast<CreateGameModuleFunc>(m_dynamicLibrary->GetSymbol("CreateGameModule"));
	m_destroyGameModule = reinterpret_cast<DestroyGameModuleFunc>(m_dynamicLibrary->GetSymbol("DestroyGameModule"));
	if (nullptr == createGameModule || nullptr == m_destroyGameModule)
	{
		result.Succeeded = false;
		result.Message = "Game module exports were not found.";
		m_state = ELiveCompileState::Failed;
		return result;
	}

	m_gameModule = createGameModule();
	if (nullptr == m_gameModule || false == m_gameModule->Initialize(m_desc.ModuleContext))
	{
		DestroyCurrentModule();
		result.Succeeded = false;
		result.Message = "Game module initialization failed.";
		m_state = ELiveCompileState::Failed;
		return result;
	}

	result.OutputLibraryPath = loadablePath.generic_string();
	m_state = ELiveCompileState::Loaded;
	return result;
}

IGameModule* CLiveCompileManager::GetGameModule() const
{
	return m_gameModule;
}

ELiveCompileState CLiveCompileManager::GetState() const
{
	return m_state;
}

File::Path CLiveCompileManager::MakeLoadableLibraryPath() const
{
	++const_cast<CLiveCompileManager*>(this)->m_reloadSerial;
	std::filesystem::path intermediate = m_desc.IntermediateDirectory.empty() ? std::filesystem::path("Intermediate/LiveCompile") : std::filesystem::path(m_desc.IntermediateDirectory);
	return File::Path(intermediate / ("GameCode_" + std::to_string(m_reloadSerial) + ".dll"));
}

void CLiveCompileManager::DestroyCurrentModule()
{
	if (m_gameModule)
	{
		m_gameModule->Finalize();
		if (m_destroyGameModule)
		{
			m_destroyGameModule(m_gameModule);
		}
		m_gameModule = nullptr;
	}
	m_destroyGameModule = nullptr;
}

#else

bool CLiveCompileManager::Initialize(const LiveCompileDesc& desc)
{
	(void)desc;
	return false;
}

void CLiveCompileManager::Finalize()
{
}

void CLiveCompileManager::Tick()
{
}

LiveCompileResult CLiveCompileManager::RebuildAndReload()
{
	LiveCompileResult result;
	result.Message = "LiveCompile is editor-only.";
	return result;
}

IGameModule* CLiveCompileManager::GetGameModule() const
{
	return nullptr;
}

ELiveCompileState CLiveCompileManager::GetState() const
{
	return ELiveCompileState::Failed;
}

File::Path CLiveCompileManager::MakeLoadableLibraryPath() const
{
	return File::NULL_PATH;
}

void CLiveCompileManager::DestroyCurrentModule()
{
}

#endif

