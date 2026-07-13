#pragma once

#include <string_view>

class CDebug final
{
public:
	CDebug() = default;
	~CDebug() = default;

	CDebug(const CDebug&) = delete;
	CDebug& operator=(const CDebug&) = delete;
	CDebug(CDebug&&) = delete;
	CDebug& operator=(CDebug&&) = delete;

	void Trace(std::string_view message) const;
	void Log(std::string_view message) const;
	void Info(std::string_view message) const;
	void Warning(std::string_view message) const;
	void Error(std::string_view message) const;
	void Critical(std::string_view message) const;
};
