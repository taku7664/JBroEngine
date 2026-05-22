#pragma once

#include "Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Editor/LiveCompile/ILiveCompileManager.h"

class CLiveCompileManager final : public ILiveCompileManager
{
public:
	bool Initialize(const LiveCompileDesc& desc) override;
	void Finalize() override;
	void Tick() override;
	LiveCompileResult RebuildAndReload() override;
	IGameModule* GetGameModule() const override;
	ELiveCompileState GetState() const override;

private:
	File::Path MakeLoadableLibraryPath() const;
	void DestroyCurrentModule();

private:
	LiveCompileDesc m_desc;
	OwnerPtr<IFileWatcher> m_sourceWatcher;
	OwnerPtr<ICompilePipeline> m_compilePipeline;
	OwnerPtr<IDynamicLibrary> m_dynamicLibrary;
	IGameModule* m_gameModule = nullptr;
	DestroyGameModuleFunc m_destroyGameModule = nullptr;
	ELiveCompileState m_state = ELiveCompileState::Idle;
	bool m_isDirty = false;
	std::chrono::steady_clock::time_point m_dirtyTime;
	std::uint64_t m_reloadSerial = 0;
};

