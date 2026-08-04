#include "pch.h"
#include "Logger.h"

#include "Core/EngineCore.h"
#include "Core/Logging/LoggerInternal.h"

#include <cstdio>

#if JBRO_PLATFORM_ANDROID
#include <android/log.h>
#endif

namespace
{
	const char* ToLevelText(ELogLevel level)
	{
		switch (level)
		{
		case ELogLevel::Trace: return "TRACE";
		case ELogLevel::Debug: return "DEBUG";
		case ELogLevel::Info: return "INFO";
		case ELogLevel::Warning: return "WARNING";
		case ELogLevel::Error: return "ERROR";
		case ELogLevel::Critical: return "CRITICAL";
		default: return "UNKNOWN";
		}
	}

	const char* ToSourceText(ELogSource source)
	{
		return ELogSource::External == source ? "EXTERNAL" : "SYSTEM";
	}

#if JBRO_PLATFORM_ANDROID
	int ToAndroidPriority(ELogLevel level)
	{
		switch (level)
		{
		case ELogLevel::Trace: return ANDROID_LOG_VERBOSE;
		case ELogLevel::Debug: return ANDROID_LOG_DEBUG;
		case ELogLevel::Info: return ANDROID_LOG_INFO;
		case ELogLevel::Warning: return ANDROID_LOG_WARN;
		case ELogLevel::Error: return ANDROID_LOG_ERROR;
		case ELogLevel::Critical: return ANDROID_LOG_FATAL;
		default: return ANDROID_LOG_INFO;
		}
	}
#endif

	std::string BuildFormattedMessage(ELogSource source, ELogLevel level, std::string_view message)
	{
		return std::format("{} : [{}] {}", ToSourceText(source), ToLevelText(level), message);
	}

	void FallbackWrite(ELogSource source, ELogLevel level, std::string_view message)
	{
		const std::string formattedMessage = BuildFormattedMessage(source, level, message);
		std::fwrite(formattedMessage.data(), 1, formattedMessage.size(), stderr);
		std::fwrite("\n", 1, 1, stderr);
#if JBRO_PLATFORM_WINDOWS
		OutputDebugStringA((formattedMessage + "\n").c_str());
#elif JBRO_PLATFORM_ANDROID
		__android_log_write(ToAndroidPriority(level), "JBroEngine", formattedMessage.c_str());
#endif
	}

	File::Path NormalizeLogFilePath(const File::Path& path)
	{
		if (path.empty())
		{
			return File::NULL_PATH;
		}

		File::Path result = path;
		if (result.extension().empty())
		{
			result.replace_extension(".jlog");
		}
		return result;
	}
}

void CLogger::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_entries.clear();
	++m_revision;
}

void CLogger::GetEntriesSnapshot(std::vector<LogEntry>& outEntries) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	outEntries = m_entries;
}

void CLogger::GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	outEntries.clear();
	for (const LogEntry& entry : m_entries)
	{
		if (entry.Level == level)
		{
			outEntries.push_back(entry);
		}
	}
}

bool CLogger::OpenFile(const File::Path& path)
{
	const File::Path logPath = NormalizeLogFilePath(path);
	if (logPath.empty())
	{
		return false;
	}

	// 지금까지 쌓인 걸 먼저 확보한다. 아래에서 잠그기 전에 스냅샷을 떠야 한다 —
	// GetEntriesSnapshot 이 같은 뮤텍스를 잡으므로 안에서 부르면 자기 자신과 교착한다.
	std::vector<LogEntry> backlog;
	GetEntriesSnapshot(backlog);

	std::error_code errorCode;
	if (false == logPath.parent_path().empty())
	{
		File::fs::create_directories(logPath.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
	}

	// 이전 실행분을 한 세대 밀어 둔다. 크래시 → 재실행이 정작 필요한 로그를 덮어쓰는 걸 막는다.
	if (File::fs::exists(logPath, errorCode))
	{
		File::Path previousPath = logPath;
		previousPath += File::FString(".prev");
		File::fs::remove(previousPath, errorCode);
		File::fs::rename(logPath, previousPath, errorCode);
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_file.is_open())
	{
		m_file.close();
	}
	m_file.open(logPath, std::ios::out | std::ios::trunc);
	if (false == m_file.is_open())
	{
		m_filePath = File::Path();
		return false;
	}
	m_filePath = logPath;

	for (const LogEntry& entry : backlog)
	{
		m_file << entry.FormattedMessage << '\n';
	}
	m_file.flush();
	return true;
}

void CLogger::CloseFile()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_file.is_open())
	{
		m_file.flush();
		m_file.close();
	}
	m_filePath = File::Path();
}

bool CLogger::IsFileOpen() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_file.is_open();
}

const File::Path& CLogger::GetFilePath() const
{
	return m_filePath;
}

bool CLogger::SaveToFile(const File::Path& path) const
{
	const File::Path logPath = NormalizeLogFilePath(path);
	if (logPath.empty())
	{
		return false;
	}

	std::vector<LogEntry> snapshot;
	GetEntriesSnapshot(snapshot);

	std::error_code errorCode;
	if (false == logPath.parent_path().empty())
	{
		std::filesystem::create_directories(logPath.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
	}

	std::ofstream stream(logPath, std::ios::out | std::ios::trunc);
	if (false == stream.is_open())
	{
		return false;
	}

	for (const LogEntry& entry : snapshot)
	{
		stream << entry.FormattedMessage << '\n';
	}
	return true;
}

std::size_t CLogger::GetEntryCount() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_entries.size();
}

std::uint64_t CLogger::GetRevision() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_revision;
}

void CLogger::Write(ELogSource source, ELogLevel level, std::string_view message)
{
	LogEntry entry;
	entry.Level = level;
	entry.Source = source;
	entry.Time = std::chrono::system_clock::now();
	entry.Message = message;
	entry.FormattedMessage = BuildFormattedMessage(source, level, message);
	const std::string formattedMessage = entry.FormattedMessage;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		entry.Sequence = m_nextSequence++;
		m_entries.push_back(std::move(entry));
		++m_revision;

		if (m_file.is_open())
		{
			// 줄마다 내린다. 이 파일이 존재하는 이유가 "죽고 난 뒤에 읽는 것"이라
			// 버퍼에 남겨 두면 정작 원인 직전의 몇 줄이 사라진다 — 크래시든 강제 종료든
			// 마지막 줄이 가장 중요하다. 레벨로 나눠 봐야 Info 로 남긴 단서를 잃을 뿐이다.
			// 비용은 게임이 프레임 루프에서 로그를 뿜지 않는다는 전제에 기댄다(그건 별도 문제).
			m_file << formattedMessage << '\n';
			m_file.flush();
		}
	}

#if JBRO_PLATFORM_WINDOWS
	OutputDebugStringA((formattedMessage + "\n").c_str());
#elif JBRO_PLATFORM_WEB
	std::fwrite(formattedMessage.data(), 1, formattedMessage.size(), stderr);
	std::fwrite("\n", 1, 1, stderr);
#elif JBRO_PLATFORM_ANDROID
	__android_log_write(ToAndroidPriority(level), "JBroEngine", formattedMessage.c_str());
#endif
}

void Log::Trace(std::string_view message)
{
	WriteExternal(ELogLevel::Trace, message);
}

void Log::Debug(std::string_view message)
{
	WriteExternal(ELogLevel::Debug, message);
}

void Log::Info(std::string_view message)
{
	WriteExternal(ELogLevel::Info, message);
}

void Log::Warning(std::string_view message)
{
	WriteExternal(ELogLevel::Warning, message);
}

void Log::Error(std::string_view message)
{
	WriteExternal(ELogLevel::Error, message);
}

void Log::Critical(std::string_view message)
{
	WriteExternal(ELogLevel::Critical, message);
}

void Log::GetEntriesSnapshot(std::vector<LogEntry>& outEntries)
{
	SafePtr<CLogger> logger = GetLogger();
	if (logger)
	{
		logger->GetEntriesSnapshot(outEntries);
	}
	else
	{
		outEntries.clear();
	}
}

void Log::GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries)
{
	SafePtr<CLogger> logger = GetLogger();
	if (logger)
	{
		logger->GetEntriesByLevel(level, outEntries);
	}
	else
	{
		outEntries.clear();
	}
}

bool Log::SaveToFile(const File::Path& path)
{
	SafePtr<CLogger> logger = GetLogger();
	return logger ? logger->SaveToFile(path) : false;
}

SafePtr<CLogger> Log::GetLogger()
{
	return Engine.Logger;
}

namespace
{
	// dll 마다 별도로 존재하는 static (Engine.lib 정적 링크 특성).
	// SetHostLogger 가 호출되면 해당 dll/exe 안에서만 라우팅이 활성화된다.
	CLogger* g_hostLogger = nullptr;
}

void Log::SetHostLogger(CLogger* logger)
{
	g_hostLogger = logger;
}

void Log::WriteExternal(ELogLevel level, std::string_view message)
{
	// 1) 호스트가 주입한 logger (DLL → 호스트 경로)
	if (g_hostLogger)
	{
		g_hostLogger->Write(ELogSource::External, level, message);
		return;
	}

	// 2) 자기 모듈의 Engine.Logger
	SafePtr<CLogger> logger = GetLogger();
	if (logger)
	{
		logger->Write(ELogSource::External, level, message);
		return;
	}

	// 3) Fallback (stderr / OutputDebugString)
	FallbackWrite(ELogSource::External, level, message);
}

void CSystemLog::Trace(std::string_view message)
{
	WriteSystem(ELogLevel::Trace, message);
}

void CSystemLog::Debug(std::string_view message)
{
	WriteSystem(ELogLevel::Debug, message);
}

void CSystemLog::Info(std::string_view message)
{
	WriteSystem(ELogLevel::Info, message);
}

void CSystemLog::Warning(std::string_view message)
{
	WriteSystem(ELogLevel::Warning, message);
}

void CSystemLog::Error(std::string_view message)
{
	WriteSystem(ELogLevel::Error, message);
}

void CSystemLog::Critical(std::string_view message)
{
	WriteSystem(ELogLevel::Critical, message);
}

void CSystemLog::WriteSystem(ELogLevel level, std::string_view message)
{
	SafePtr<CLogger> logger = Engine.Logger;
	if (logger)
	{
		logger->Write(ELogSource::System, level, message);
		return;
	}

	FallbackWrite(ELogSource::System, level, message);
}
