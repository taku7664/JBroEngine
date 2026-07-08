#include "pch.h"
#include "ImReferenceField.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Icons/FontAwesomeIcons.h"
#include "Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImIconButton.h"
#include "Editor/ImItem/ImItemLocalizationKeys.h"

#include <algorithm>

ImReferenceField::ImReferenceField(const char* id, std::string label, bool isNull)
	: m_id(id)
	, m_label(std::move(label))
	, m_isNull(isNull)
{
}

ImReferenceField& ImReferenceField::Width(float width)
{
	m_width = width;
	return *this;
}

ImReferenceField& ImReferenceField::Tooltip(std::string text)
{
	m_tooltip = std::move(text);
	return *this;
}

ImReferenceField& ImReferenceField::ClearTooltip(const char* text)
{
	m_clearTooltip = text;
	return *this;
}

ImReferenceField& ImReferenceField::AllowClear(bool allowClear)
{
	m_allowClear = allowClear;
	return *this;
}

ImReferenceField& ImReferenceField::OnAcceptDrop(Callback callback)
{
	m_acceptDrop = std::move(callback);
	return *this;
}

ImReferenceField& ImReferenceField::OnClear(VoidCallback callback)
{
	m_clear = std::move(callback);
	return *this;
}

bool ImReferenceField::Draw() const
{
	bool changed = false;
	ImGui::PushID(nullptr != m_id ? m_id : "");

	const float clearW = ImGui::GetFrameHeight();
	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float fieldW = m_allowClear
		? std::max(1.0f, fullW - clearW - ImGui::GetStyle().ItemSpacing.x)
		: std::max(1.0f, fullW);

	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	const std::string buttonLabel = m_label.empty()
		? std::string("##reference_button")
		: m_label + "##reference_button";
	ImGui::Button(buttonLabel.c_str(), ImVec2(fieldW, 0.0f));
	ImGui::PopStyleColor(3);

	if (m_acceptDrop && m_acceptDrop())
	{
		changed = true;
	}

	if (false == m_tooltip.empty())
	{
		ImGui::Utillity::HoveredToolTip(m_tooltip.c_str());
	}

	if (m_allowClear)
	{
		ImGui::SameLine();
		if (ImIconButton("clear", EditorIcons::ICON_X_MARK)
			.Size(ImVec2(clearW, 0.0f))
			.Tooltip(nullptr != m_clearTooltip ? m_clearTooltip : Loc::Text(ImItemLocKeys::CommonRemove))
			.Disabled(m_isNull)
			.Draw())
		{
			if (m_clear)
			{
				m_clear();
			}
			changed = true;
		}
	}

	ImGui::PopID();
	return changed;
}

#endif
