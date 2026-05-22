#pragma once

#include "Engine/Editor/FileSystem/IFileWatcher.h"

#include <unordered_map>

class CWindowsFileWatcher final : public IFileWatcher
{
public:
	bool Watch(const FileWatcherDesc& desc) override;
	void Stop() override;
	void Poll() override;
	bool TakeEvents(std::vector<FileWatchEvent>& outEvents) override;
	bool IsWatching() const override;

private:
	void BuildSnapshot(std::unordered_map<File::Path, std::filesystem::file_time_type>& outSnapshot) const;

private:
	FileWatcherDesc m_desc;
	std::unordered_map<File::Path, std::filesystem::file_time_type> m_snapshot;
	std::vector<FileWatchEvent> m_pendingEvents;
	bool m_isWatching = false;
};

