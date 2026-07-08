#include "pch.h"
#include "ImStatusBadge.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <algorithm>

ImStatusBadge::ImStatusBadge(const char* text)
	: m_text(text)
{
}

ImStatusBadge& ImStatusBadge::Severity(EImValidationSeverity severity)
{
	m_severity = severity;
	return *this;
}

ImStatusBadge& ImStatusBadge::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImStatusBadge& ImStatusBadge::MinWidth(float width)
{
	m_minWidth = width;
	return *this;
}

void ImStatusBadge::Draw() const
{
	const char* text = nullptr != m_text ? m_text : "";
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 textSize = ImGui::CalcTextSize(text);
	const ImVec2 size(
		std::max(m_minWidth, textSize.x + style.FramePadding.x * 2.0f),
		textSize.y + style.FramePadding.y * 2.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();

	ImGui::PushID(this);
	ImGui::InvisibleButton("##status_badge", size);

	const ImVec4 base = ImItem::ValidationColor(m_severity);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(
		pos,
		ImVec2(pos.x + size.x, pos.y + size.y),
		ImGui::GetColorU32(ImItem::WithAlpha(base, 0.18f)),
		style.FrameRounding);
	drawList->AddRect(
		pos,
		ImVec2(pos.x + size.x, pos.y + size.y),
		ImGui::GetColorU32(ImItem::WithAlpha(base, 0.65f)),
		style.FrameRounding);
	drawList->AddText(
		ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y),
		ImGui::GetColorU32(base),
		text);

	if (false == ImItem::IsEmptyText(m_tooltip) && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", m_tooltip);
	}
	ImGui::PopID();
}

#endif
