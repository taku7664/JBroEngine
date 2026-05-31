#include "pch.h"
#include "WindowsFileWatcher.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

bool CWindowsFileWatcher::Watch(const FileWatcherDesc& desc)
{
	Stop();
	if (desc.RootPath.empty())
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(desc.RootPath, errorCode) || false == std::filesystem::is_directory(desc.RootPath, errorCode))
	{
		return false;
	}

	m_desc = desc;
	BuildSnapshot(m_snapshot);
	m_isWatching = true;
	return true;
}

void CWindowsFileWatcher::Stop()
{
	m_snapshot.clear();
	m_pendingEvents.clear();
	m_isWatching = false;
}

void CWindowsFileWatcher::Poll()
{
	if (false == m_isWatching)
	{
		return;
	}

	std::unordered_map<File::Path, WatchedFileStat> nextSnapshot;
	BuildSnapshot(nextSnapshot);

	// 직전엔 파일이 여럿이었는데 이번 스냅샷이 비었다면, 루트가 잠시 접근 불가/잠김
	// (외부 탐색기·안티바이러스·인덱서가 트리를 잠그는 순간)일 가능성이 크다. 전체
	// 삭제로 오인해 모든 자산을 언레지스터하면 참조가 깨지므로, 이번 폴은 건너뛴다.
	// (진짜로 전부 지운 경우는 다음 프로젝트 로드의 reconcile 이 정리한다 — 안전한 쪽.)
	if (nextSnapshot.empty() && m_snapshot.size() > 4)
	{
		return;
	}

	for (const auto& pair : nextSnapshot)
	{
		auto oldIt = m_snapshot.find(pair.first);
		if (oldIt == m_snapshot.end())
		{
			m_pendingEvents.push_back({ EFileWatchEventType::Created, pair.first, File::NULL_PATH });
			continue;
		}

		if (oldIt->second != pair.second)
		{
			m_pendingEvents.push_back({ EFileWatchEventType::Modified, pair.first, File::NULL_PATH });
		}
	}

	for (const auto& pair : m_snapshot)
	{
		if (nextSnapshot.find(pair.first) == nextSnapshot.end())
		{
			m_pendingEvents.push_back({ EFileWatchEventType::Deleted, pair.first, File::NULL_PATH });
		}
	}

	m_snapshot = std::move(nextSnapshot);
}

bool CWindowsFileWatcher::TakeEvents(std::vector<FileWatchEvent>& outEvents)
{
	outEvents = std::move(m_pendingEvents);
	m_pendingEvents.clear();
	return false == outEvents.empty();
}

bool CWindowsFileWatcher::IsWatching() const
{
	return m_isWatching;
}

void CWindowsFileWatcher::BuildSnapshot(std::unordered_map<File::Path, WatchedFileStat>& outSnapshot) const
{
	outSnapshot.clear();

	// 한 항목(잠긴 파일/권한 거부 등)에서 멈추지 않도록 increment(ec) 로 직접 순회하며
	// 오류는 건너뛴다.  (예전엔 break 라서 파일 하나가 전체 스냅샷을 중단시켰다.)
	const std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
	std::error_code errorCode;

	auto record = [&outSnapshot](const std::filesystem::directory_entry& entry)
	{
		std::error_code statEc;
		if (false == entry.is_regular_file(statEc) || statEc)
		{
			return;
		}
		WatchedFileStat stat;
		stat.Size  = entry.file_size(statEc);
		if (statEc) { statEc.clear(); return; }   // 쓰기 중/잠금 — 다음 폴에서 다시.
		stat.Mtime = entry.last_write_time(statEc);
		if (statEc) { statEc.clear(); return; }
		outSnapshot.emplace(File::Path(entry.path()), stat);
	};

	if (m_desc.Recursive)
	{
		std::filesystem::recursive_directory_iterator it(m_desc.RootPath, options, errorCode), end;
		for (; it != end; it.increment(errorCode))
		{
			if (errorCode) { errorCode.clear(); continue; }
			record(*it);
		}
		return;
	}

	std::filesystem::directory_iterator it(m_desc.RootPath, options, errorCode), end;
	for (; it != end; it.increment(errorCode))
	{
		if (errorCode) { errorCode.clear(); continue; }
		record(*it);
	}
}

#else

bool CWindowsFileWatcher::Watch(const FileWatcherDesc& desc)
{
	(void)desc;
	return false;
}

void CWindowsFileWatcher::Stop()
{
}

void CWindowsFileWatcher::Poll()
{
}

bool CWindowsFileWatcher::TakeEvents(std::vector<FileWatchEvent>& outEvents)
{
	outEvents.clear();
	return false;
}

bool CWindowsFileWatcher::IsWatching() const
{
	return false;
}

void CWindowsFileWatcher::BuildSnapshot(std::unordered_map<File::Path, std::filesystem::file_time_type>& outSnapshot) const
{
	outSnapshot.clear();
}

#endif

