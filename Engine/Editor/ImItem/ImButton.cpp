#include "pch.h"
#include "ImButton.h"
#include "Editor/ImGuiUtillity.h"

bool ImTextButton(const char* label, const ImVec2& size, const ImVec2& offset, ImGuiButtonFlags flags)
{
	const ImVec2 startCursor = ImGui::GetCursorPos();
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	ImGui::Utillity::StyleBuilder styleBuilder;
	styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 100));
	styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
	ImGui::PushID(label);
	bool pressed = ImGui::ButtonEx("", size, flags);
	const ImVec2 buttonSize = ImGui::GetItemRectSize();
	ImGui::SameLine();
	if (ImGui::IsItemHovered())
	{
		styleBuilder.PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg));
	}
	const ImVec2 textPos = startCursor + (buttonSize - textSize) * 0.5f + offset;
	ImGui::SetCursorPos(textPos);
	ImGui::TextUnformatted(label);
	ImGui::PopID();
	return pressed;
}

bool ImTextButton(ImText text, const char* label, const ImVec2& size, const ImVec2& offset, ImGuiButtonFlags flags)
{
	const float scale = (text.GetScale() > 0.0f) ? text.GetScale() : 1.0f;

	const ImVec2 startCursor = ImGui::GetCursorPos();
	ImGui::Utillity::StyleBuilder styleBuilder;
	styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 100));
	styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
	ImGui::PushID(label);
	const bool pressed = ImGui::ButtonEx("", size, flags);
	const ImVec2 buttonSize = ImGui::GetItemRectSize();

	if (ImGui::IsItemHovered())
	{
		styleBuilder.PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg));
	}

	// 스케일 1 에서 측정 후 scale 을 곱해 실제 렌더 크기를 구한다(이중 스케일 방지).
	const ImVec2 baseSize   = ImGui::CalcTextSize(label);
	const ImVec2 scaledSize = ImVec2(baseSize.x * scale, baseSize.y * scale);

	float weightX = 0.5f;
	switch (text.GetAlign())
	{
	case ImText::Align::Left:  weightX = 0.0f; break;
	case ImText::Align::Right: weightX = 1.0f; break;
	default:                   weightX = 0.5f; break;
	}

	const ImVec2 textPos = startCursor
		+ ImVec2((buttonSize.x - scaledSize.x) * weightX, (buttonSize.y - scaledSize.y) * 0.5f)
		+ offset;
	ImGui::SetCursorPos(textPos);
	ImGui::SetWindowFontScale(scale);
	ImGui::TextUnformatted(label);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopID();
	return pressed;
}
