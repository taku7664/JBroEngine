#pragma once

#include "Engine/Editor/FileSystem/IFileWatcher.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

// 파일 식별 상태 — 변경 감지를 mtime 만이 아니라 (size, mtime) 로 한다.
// 콘텐츠를 같은 mtime 으로 교체(드물지만 가능)해도 크기가 다르면 Modified 를 잡는다.
struct WatchedFileStat
{
	std::uintmax_t                  Size = 0;
	std::filesystem::file_time_type Mtime{};

	bool operator==(const WatchedFileStat& other) const { return Size == other.Size && Mtime == other.Mtime; }
	bool operator!=(const WatchedFileStat& other) const { return false == (*this == other); }
};

class CWindowsFileWatcher final : public IFileWatcher
{
public:
	bool Watch(const FileWatcherDesc& desc) override;
	void Stop() override;
	void Poll() override;
	bool TakeEvents(std::vector<FileWatchEvent>& outEvents) override;
	bool IsWatching() const override;

private:
	void BuildSnapshot(std::unordered_map<File::Path, WatchedFileStat>& outSnapshot) const;

private:
	FileWatcherDesc m_desc;
	std::unordered_map<File::Path, WatchedFileStat> m_snapshot;
	std::vector<FileWatchEvent> m_pendingEvents;
	bool m_isWatching = false;
};

