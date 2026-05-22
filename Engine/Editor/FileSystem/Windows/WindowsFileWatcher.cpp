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

	std::unordered_map<File::Path, std::filesystem::file_time_type> nextSnapshot;
	BuildSnapshot(nextSnapshot);

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

void CWindowsFileWatcher::BuildSnapshot(std::unordered_map<File::Path, std::filesystem::file_time_type>& outSnapshot) const
{
	outSnapshot.clear();

	std::error_code errorCode;
	if (m_desc.Recursive)
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(m_desc.RootPath, errorCode))
		{
			if (errorCode)
			{
				errorCode.clear();
				break;
			}

			if (entry.is_regular_file(errorCode))
			{
				outSnapshot.emplace(File::Path(entry.path()), entry.last_write_time(errorCode));
			}
		}
		return;
	}

	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_desc.RootPath, errorCode))
	{
		if (errorCode)
		{
			errorCode.clear();
			break;
		}

		if (entry.is_regular_file(errorCode))
		{
			outSnapshot.emplace(File::Path(entry.path()), entry.last_write_time(errorCode));
		}
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

