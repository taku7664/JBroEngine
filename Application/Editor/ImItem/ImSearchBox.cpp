#include "pch.h"
#include "ImSearchBox.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Icons/FontAwesomeIcons.h"
#include "Editor/ImItem/ImIconButton.h"
#include "Editor/ImItem/ImItemLocalizationKeys.h"

#include <algorithm>

ImSearchBox::ImSearchBox(const char* id, std::string& text)
	: m_id(id)
	, m_text(text)
{
}

ImSearchBox& ImSearchBox::Hint(const char* text)
{
	m_hint = text;
	return *this;
}

ImSearchBox& ImSearchBox::Width(float width)
{
	m_width = width;
	return *this;
}

ImSearchBox& ImSearchBox::ShowClear(bool show)
{
	m_showClear = show;
	return *this;
}

ImSearchBox& ImSearchBox::ClearTooltip(const char* text)
{
	m_clearTooltip = text;
	return *this;
}

ImSearchBox& ImSearchBox::Flags(ImGuiInputTextFlags flags)
{
	m_flags = flags;
	return *this;
}

bool ImSearchBox::Draw() const
{
	bool changed = false;
	ImGui::PushID(nullptr != m_id ? m_id : "##search_box");

	const bool drawClear = m_showClear && false == m_text.empty();
	const float clearW = drawClear ? ImGui::GetFrameHeight() : 0.0f;
	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float fieldW = drawClear
		? std::max(1.0f, fullW - clearW - ImGui::GetStyle().ItemSpacing.x)
		: std::max(1.0f, fullW);

	ImGui::SetNextItemWidth(fieldW);
	if (ImGui::InputTextWithHint(
		"##input",
		nullptr != m_hint ? m_hint : Loc::Text(ImItemLocKeys::CommonSearch),
		&m_text,
		m_flags))
	{
		changed = true;
	}

	if (drawClear)
	{
		ImGui::SameLine();
		if (ImIconButton("clear", EditorIcons::ICON_X_MARK)
			.Size(ImVec2(clearW, 0.0f))
			.Tooltip(nullptr != m_clearTooltip ? m_clearTooltip : Loc::Text(ImItemLocKeys::CommonRemove))
			.Draw())
		{
			m_text.clear();
			changed = true;
		}
	}

	ImGui::PopID();
	return changed;
}

#endif
