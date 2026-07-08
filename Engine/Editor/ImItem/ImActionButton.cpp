#include "pch.h"
#include "ImActionButton.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

ImActionButton::ImActionButton(const char* label)
	: m_label(label)
{
}

ImActionButton& ImActionButton::Severity(EImValidationSeverity severity)
{
	m_severity = severity;
	return *this;
}

ImActionButton& ImActionButton::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImActionButton& ImActionButton::Size(ImVec2 size)
{
	m_size = size;
	return *this;
}

ImActionButton& ImActionButton::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

bool ImActionButton::Draw() const
{
	const bool styled = EImValidationSeverity::Info != m_severity;
	if (styled)
	{
		const ImVec4 base = ImItem::ValidationColor(m_severity);
		ImGui::PushStyleColor(ImGuiCol_Button, ImItem::WithAlpha(base, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImItem::WithAlpha(ImItem::ScaleColor(base, 1.12f), 0.72f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImItem::WithAlpha(ImItem::ScaleColor(base, 0.92f), 0.85f));
	}
	if (m_disabled)
	{
		ImGui::BeginDisabled();
	}

	const bool clicked = ImGui::Button(nullptr != m_label ? m_label : "", m_size);

	if (m_disabled)
	{
		ImGui::EndDisabled();
	}
	if (styled)
	{
		ImGui::PopStyleColor(3);
	}
	ImItem::HoveredTooltip(m_tooltip);
	return clicked && false == m_disabled;
}

#endif
