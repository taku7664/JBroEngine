#include "pch.h"
#include "ImValidationMessage.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

ImValidationMessage::ImValidationMessage(const char* message, EImValidationSeverity severity)
	: m_message(message)
	, m_severity(severity)
{
}

ImValidationMessage& ImValidationMessage::Wrap(bool wrap)
{
	m_wrap = wrap;
	return *this;
}

void ImValidationMessage::Draw() const
{
	if (ImItem::IsEmptyText(m_message))
	{
		return;
	}

	const std::string text = std::string(ImItem::ValidationPrefix(m_severity)) + m_message;
	ImGui::PushStyleColor(ImGuiCol_Text, ImItem::ValidationColor(m_severity));
	if (m_wrap)
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopTextWrapPos();
	}
	else
	{
		ImGui::TextUnformatted(text.c_str());
	}
	ImGui::PopStyleColor();
}

#endif
