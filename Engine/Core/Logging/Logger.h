#pragma once

#include "LoggerTypes.h"
#include "File/FilePath.h"
#include "Utillity/SafePtr.h"

#include <mutex>
#include <string_view>
#include <vector>

class CSystemLog;

class CLogger final
{
public:
	CLogger() = default;
	~CLogger() = default;

	CLogger(const CLogger&) = delete;
	CLogger& operator=(const CLogger&) = delete;
	CLogger(CLogger&&) = delete;
	CLogger& operator=(CLogger&&) = delete;

public:
	void Clear();
	void GetEntriesSnapshot(std::vector<LogEntry>& outEntries) const;
	void GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries) const;
	bool SaveToFile(const File::Path& path) const;

	std::size_t GetEntryCount() const;
	std::uint64_t GetRevision() const;

private:
	void Write(ELogSource source, ELogLevel level, std::string_view message);

private:
	friend class Log;
	friend class CSystemLog;

	mutable std::mutex m_mutex;
	std::vector<LogEntry> m_entries;
	std::uint64_t m_nextSequence = 1;
	std::uint64_t m_revision = 0;
};

class Log final
{
public:
	static void Trace(std::string_view message);
	static void Debug(std::string_view message);
	static void Info(std::string_view message);
	static void Warning(std::string_view message);
	static void Error(std::string_view message);
	static void Critical(std::string_view message);

	static void GetEntriesSnapshot(std::vector<LogEntry>& outEntries);
	static void GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries);
	static bool SaveToFile(const File::Path& path);
	static SafePtr<CLogger> GetLogger();

private:
	static void WriteExternal(ELogLevel level, std::string_view message);
};
