#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Logging/LoggerTypes.h"

#include <array>
#include <vector>

class CLogTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CLogTool() = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;

private:
	bool PassFilter(ELogLevel level) const;
	const char* GetLevelName(ELogLevel level) const;
	ImVec4 GetLevelColor(ELogLevel level) const;
	void DrawToolbar();
	void DrawEntries();

private:
	std::array<bool, 6> m_levelFilters = { true, true, true, true, true, true };
	std::vector<LogEntry> m_entries;
	std::uint64_t m_seenRevision = 0;
	bool m_autoScroll = true;
	bool m_requestScrollToBottom = false;
};

#endif
