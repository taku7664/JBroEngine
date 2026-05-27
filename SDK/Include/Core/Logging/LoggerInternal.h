#pragma once

#include "Logger.h"

class CSystemLog final
{
public:
	static void Trace(std::string_view message);
	static void Debug(std::string_view message);
	static void Info(std::string_view message);
	static void Warning(std::string_view message);
	static void Error(std::string_view message);
	static void Critical(std::string_view message);

private:
	static void WriteSystem(ELogLevel level, std::string_view message);
};
