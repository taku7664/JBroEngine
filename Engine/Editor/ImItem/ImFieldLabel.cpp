#include "pch.h"
#include "ImFieldLabel.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

namespace
{
	const ImVec4 LABEL_DISABLED_COLOR(0.55f, 0.55f, 0.55f, 1.0f);
	const ImVec4 LABEL_INVALID_COLOR(0.95f, 0.35f, 0.30f, 1.0f);
	const ImVec4 REQUIRED_MARK_COLOR(0.95f, 0.35f, 0.30f, 1.0f);
}

ImFieldLabel::ImFieldLabel(const char* text)
	: m_text(text)
{
}

ImFieldLabel& ImFieldLabel::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImFieldLabel& ImFieldLabel::Required(bool required)
{
	m_required = required;
	return *this;
}

ImFieldLabel& ImFieldLabel::Invalid(bool invalid)
{
	m_invalid = invalid;
	return *this;
}

ImFieldLabel& ImFieldLabel::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

void ImFieldLabel::Draw() const
{
	if (m_disabled)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, LABEL_DISABLED_COLOR);
	}
	else if (m_invalid)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, LABEL_INVALID_COLOR);
	}

	ImGui::TextUnformatted(nullptr != m_text ? m_text : "");

	if (m_disabled || m_invalid)
	{
		ImGui::PopStyleColor();
	}

	ImItem::HoveredTooltip(m_tooltip);

	if (m_required)
	{
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::TextColored(REQUIRED_MARK_COLOR, "*");
	}
}

#endif
