#include "pch.h"
#include "ImIconButton.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

ImIconButton::ImIconButton(const char* id, const char* icon)
	: m_id(id)
	, m_icon(icon)
{
}

ImIconButton& ImIconButton::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImIconButton& ImIconButton::Size(ImVec2 size)
{
	m_size = size;
	return *this;
}

ImIconButton& ImIconButton::Selected(bool selected)
{
	m_selected = selected;
	return *this;
}

ImIconButton& ImIconButton::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

bool ImIconButton::Draw() const
{
	bool clicked = false;
	ImGui::PushID(nullptr != m_id ? m_id : "");
	if (m_selected)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
	}
	if (m_disabled)
	{
		ImGui::BeginDisabled();
	}

	clicked = ImGui::Button(nullptr != m_icon ? m_icon : "", m_size);

	if (m_disabled)
	{
		ImGui::EndDisabled();
	}
	if (m_selected)
	{
		ImGui::PopStyleColor(3);
	}
	ImItem::HoveredTooltip(m_tooltip);
	ImGui::PopID();
	return clicked && false == m_disabled;
}

#endif
