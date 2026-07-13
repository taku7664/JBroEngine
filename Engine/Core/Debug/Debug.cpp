#include "pch.h"
#include "Debug.h"

#include "Core/Logging/Logger.h"

void CDebug::Trace(std::string_view message) const
{
	Log::Trace(message);
}

void CDebug::Log(std::string_view message) const
{
	Log::Info(message);
}

void CDebug::Info(std::string_view message) const
{
	Log::Info(message);
}

void CDebug::Warning(std::string_view message) const
{
	Log::Warning(message);
}

void CDebug::Error(std::string_view message) const
{
	Log::Error(message);
}

void CDebug::Critical(std::string_view message) const
{
	Log::Critical(message);
}
