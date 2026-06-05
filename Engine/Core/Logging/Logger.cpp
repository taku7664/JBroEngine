#include "pch.h"
#include "Logger.h"

#include "Core/EngineCore.h"
#include "Core/Logging/LoggerInternal.h"

#include <cstdio>

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
			result.replace_extension(".Jlog");
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
	}

#if JBRO_PLATFORM_WINDOWS
	OutputDebugStringA((formattedMessage + "\n").c_str());
#elif JBRO_PLATFORM_WEB
	std::fwrite(formattedMessage.data(), 1, formattedMessage.size(), stderr);
	std::fwrite("\n", 1, 1, stderr);
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
