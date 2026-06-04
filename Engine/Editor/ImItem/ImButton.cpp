#include "pch.h"
#include "ImButton.h"
#include "Editor/ImGuiUtillity.h"

bool ImTextButton(const char* label, const ImVec2& size, const ImVec2& offset, ImGuiButtonFlags flags)
{
	const ImVec2 startCursor = ImGui::GetCursorPos();
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	ImGui::Utillity::StyleBuilder styleBuilder;
	styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
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
