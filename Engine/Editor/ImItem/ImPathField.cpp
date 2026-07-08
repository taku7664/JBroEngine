#include "pch.h"
#include "ImPathField.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

#include <algorithm>

ImPathField::ImPathField(const char* id, std::string& path)
	: m_id(id)
	, m_path(path)
{
}

ImPathField& ImPathField::Hint(const char* text)
{
	m_hint = text;
	return *this;
}

ImPathField& ImPathField::Width(float width)
{
	m_width = width;
	return *this;
}

ImPathField& ImPathField::ReserveTrailingWidth(float width)
{
	m_reserveTrailingWidth = width;
	return *this;
}

ImPathField& ImPathField::ReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	return *this;
}

ImPathField& ImPathField::Invalid(bool invalid)
{
	m_invalid = invalid;
	return *this;
}

ImPathField& ImPathField::Flags(ImGuiInputTextFlags flags)
{
	m_flags = flags;
	return *this;
}

bool ImPathField::Draw() const
{
	ImGui::PushID(nullptr != m_id ? m_id : "##path_field");

	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float reservedW = std::max(0.0f, m_reserveTrailingWidth);
	const float fieldW = reservedW > 0.0f
		? std::max(1.0f, fullW - reservedW - ImGui::GetStyle().ItemInnerSpacing.x)
		: std::max(1.0f, fullW);
	ImGui::SetNextItemWidth(fieldW);

	const ImGuiInputTextFlags flags = m_flags | (m_readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);
	ImItem::PushInvalidFrameStyle(m_invalid);
	const bool changed = ImItem::IsEmptyText(m_hint)
		? ImGui::InputText("##path", &m_path, flags)
		: ImGui::InputTextWithHint("##path", m_hint, &m_path, flags);
	ImItem::PopInvalidFrameStyle(m_invalid);

	ImGui::PopID();
	return changed;
}

#endif
