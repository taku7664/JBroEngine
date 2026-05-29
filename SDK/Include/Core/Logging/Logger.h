#pragma once

#include "LoggerTypes.h"

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

	// ── Host logger 주입 ──────────────────────────────────────────────────────
	// GameScript 같은 DLL 은 Engine.lib 을 정적 링크하므로 Core::Logger static SafePtr
	// 가 dll 안에 별도 인스턴스로 존재 → 호스트(Application)의 Logger 와 분리된다.
	// 호스트가 자신의 CLogger* 를 SetHostLogger 로 주입하면 dll 측 Log::Debug 등의
	// 호출이 호스트 Logger 로 라우팅되어 LogTool 에 즉시 표시된다.
	// nullptr 전달 시 라우팅 해제 (Core::Logger 또는 fallback 으로 복귀).
	static void SetHostLogger(CLogger* logger);

private:
	static void WriteExternal(ELogLevel level, std::string_view message);
};
