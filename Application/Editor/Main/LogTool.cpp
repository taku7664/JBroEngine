#include "pch.h"
#include "LogTool.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Logging/Logger.h"
#include "StringUtillity.h"

void CLogTool::OnCreate()
{
	SetTitle("Log");
}

void CLogTool::OnRenderStay()
{
	SafePtr<CLogger> logger = Log::GetLogger();
	const std::uint64_t revision = logger ? logger->GetRevision() : 0;
	if (revision != m_seenRevision)
	{
		m_seenRevision = revision;
		Log::GetEntriesSnapshot(m_entries);
		m_requestScrollToBottom = m_autoScroll;
	}

	DrawToolbar();
	ImGui::Separator();
	DrawEntries();
}

bool CLogTool::PassFilter(ELogLevel level) const
{
	const std::size_t index = static_cast<std::size_t>(level);
	return index < m_levelFilters.size() && m_levelFilters[index];
}

const char* CLogTool::GetLevelName(ELogLevel level) const
{
	switch (level)
	{
	case ELogLevel::Trace: return "Trace";
	case ELogLevel::Debug: return "Debug";
	case ELogLevel::Info: return "Info";
	case ELogLevel::Warning: return "Warning";
	case ELogLevel::Error: return "Error";
	case ELogLevel::Critical: return "Critical";
	default: return "Unknown";
	}
}

ImVec4 CLogTool::GetLevelColor(ELogLevel level) const
{
	switch (level)
	{
	case ELogLevel::Trace: return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
	case ELogLevel::Debug: return ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
	case ELogLevel::Info: return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
	case ELogLevel::Warning: return ImVec4(1.0f, 0.78f, 0.26f, 1.0f);
	case ELogLevel::Error: return ImVec4(1.0f, 0.33f, 0.28f, 1.0f);
	case ELogLevel::Critical: return ImVec4(1.0f, 0.15f, 0.58f, 1.0f);
	default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CLogTool::DrawToolbar()
{
	ImGui::TextUnformatted("Filter");
	ImGui::SameLine();
	for (std::size_t i = 0; i < m_levelFilters.size(); ++i)
	{
		const ELogLevel level = static_cast<ELogLevel>(i);
		ImGui::PushID(static_cast<int>(i));
		ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(level));
		ImGui::Checkbox(GetLevelName(level), &m_levelFilters[i]);
		ImGui::PopStyleColor();
		ImGui::PopID();
		if (i + 1 < m_levelFilters.size())
		{
			ImGui::SameLine();
		}
	}

	ImGui::SameLine();
	ImGui::Checkbox("Auto Scroll", &m_autoScroll);
}

void CLogTool::DrawEntries()
{
	if (ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_entries.size()));
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const LogEntry& entry = m_entries[static_cast<std::size_t>(row)];
				if (false == PassFilter(entry.Level))
				{
					continue;
				}

				ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(entry.Level));
				ImGui::TextUnformatted(entry.FormattedMessage.c_str());
				ImGui::PopStyleColor();
			}
		}

		if (m_requestScrollToBottom)
		{
			ImGui::SetScrollHereY(1.0f);
			m_requestScrollToBottom = false;
		}
	}
	ImGui::EndChild();
}

#endif
