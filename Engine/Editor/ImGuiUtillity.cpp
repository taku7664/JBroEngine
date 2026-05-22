#include "pch.h"
#include "ImGuiUtillity.h"

bool ImGui::Utillity::IsWindowDrawable(ImGuiWindow* window)
{
    if (!window)
        window = ImGui::GetCurrentWindowRead();
    if (!window)
        return false;

    return !window->SkipItems;
}

void ImGui::Utillity::TextWithVerticalSeparator( const char* text , float startX )
{
	ImGui::Text( text );
	if ( FLT_MAX == startX )
	{
		startX = ImGui::GetCursorPosX();
		startX += ImGui::CalcTextSize( text ).x;
		startX += ImGui::GetStyle().ItemSpacing.x;
	}
	ImGui::SameLine( startX );
	ImGui::SeparatorEx( ImGuiSeparatorFlags_Vertical );
	ImGui::SameLine();

	float availX = ImGui::GetContentRegionAvail().x;
	ImGui::SetNextItemWidth( availX );
}

ImVec4 ImGui::Utillity::ColorFromGuid( const GUID& guid )
{
	float r = guid.Data1 % 256 / 255.0f;
	float g = guid.Data2 % 256 / 255.0f;
	float b = guid.Data3 % 256 / 255.0f;

	// Keep generated colors in a visible brightness range.
	const float minBrightness = 0.5f;
	r = minBrightness + r * ( 1.0f - minBrightness );
	g = minBrightness + g * ( 1.0f - minBrightness );
	b = minBrightness + b * ( 1.0f - minBrightness );

	return ImVec4( r , g , b , 1.0f);
}

bool ImGui::Utillity::HoveredToolTip( const char* toolTip , int flags)
{
	bool isHovered = ImGui::IsItemHovered(flags);
	if (isHovered)
	{
		ImGui::SetTooltip(toolTip);
	}
	return isHovered;
}

bool ImGui::Utillity::Checkbox(const char* label, bool* v, CheckMarkType checkType)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

	const float square_sz = ImGui::GetFrameHeight();
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + style.FramePadding.y * 2.0f));
	ImGui::ItemSize(total_bb, style.FramePadding.y);
	const bool is_visible = ImGui::ItemAdd(total_bb, id);
	const bool is_multi_select = (g.LastItemData.ItemFlags & ImGuiItemFlags_IsMultiSelect) != 0;
	if (!is_visible)
		if (!is_multi_select || !g.BoxSelectState.UnclipMode || !g.BoxSelectState.UnclipRect.Overlaps(total_bb)) // Extra layer of "no logic clip" for box-select support
		{
			IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
			return false;
		}

	// Range-Selection/Multi-selection support (header)
	bool checked = *v;
	if (is_multi_select)
		ImGui::MultiSelectItemHeader(id, &checked, NULL);

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);

	// Range-Selection/Multi-selection support (footer)
	if (is_multi_select)
		ImGui::MultiSelectItemFooter(id, &checked, &pressed);
	else if (pressed)
		checked = !checked;

	if (*v != checked)
	{
		*v = checked;
		pressed = true; // return value
		ImGui::MarkItemEdited(id);
	}

	const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));
	const bool mixed_value = (g.LastItemData.ItemFlags & ImGuiItemFlags_MixedValue) != 0;
	if (is_visible)
	{
		ImGui::RenderNavCursor(total_bb, id);
		ImGui::RenderFrame(check_bb.Min, check_bb.Max, ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), true, style.FrameRounding);
		ImU32 check_col = ImGui::GetColorU32(ImGuiCol_CheckMark);
		if (mixed_value)
		{
			// Undocumented tristate/mixed/indeterminate checkbox (#2644)
			// This may seem awkwardly designed because the aim is to make ImGuiItemFlags_MixedValue supported by all widgets (not just checkbox)
			ImVec2 pad(ImMax(1.0f, IM_TRUNC(square_sz / 3.6f)), ImMax(1.0f, IM_TRUNC(square_sz / 3.6f)));
			window->DrawList->AddRectFilled(check_bb.Min + pad, check_bb.Max - pad, check_col, style.FrameRounding);
		}
		else if (*v)
		{
			switch (checkType)
			{
			case ImGui::Utillity::CheckMarkType::Check:
			{
				const float pad = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
				ImGui::RenderCheckMark(window->DrawList, check_bb.Min + ImVec2(pad, pad), check_col, square_sz - pad * 2.0f);
				break;
			}
			case ImGui::Utillity::CheckMarkType::X:
			{
				ImGui::RenderXMark(window->DrawList, check_bb.Min, check_bb.Max);
				break;
			}
			case ImGui::Utillity::CheckMarkType::Circle:
			{
				const float pad = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
				ImVec2 min = check_bb.Min + ImVec2(pad, pad);
				ImVec2 max = check_bb.Max - ImVec2(pad, pad);
				ImGui::RenderCircleMark(window->DrawList, min, max, 2.0f);
				break;
			}
			default:
				break;
			}
		}
	}
	const ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.FramePadding.y);
	if (g.LogEnabled)
		ImGui::LogRenderedText(&label_pos, mixed_value ? "[~]" : *v ? "[x]" : "[ ]");
	if (is_visible && label_size.x > 0.0f)
		ImGui::RenderText(label_pos, label);

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
	return pressed;
}

void ImGui::RenderXMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, float thickness)
{
	ImU32 col = ImGui::GetColorU32(ImGuiCol_CheckMark);
	drawList->AddLine(min, max, col, thickness);
	drawList->AddLine(
		ImVec2(min.x, max.y),
		ImVec2(max.x, min.y),
		col, thickness
	);
}

void ImGui::RenderCircleMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, float thickness)
{
	ImU32 col = ImGui::GetColorU32(ImGuiCol_CheckMark);

	ImVec2 center = ImVec2(
		(min.x + max.x) * 0.5f,
		(min.y + max.y) * 0.5f
	);

	float radius = ImMin(max.x - min.x, max.y - min.y) * 0.5f - thickness;

	drawList->AddCircle(center, radius, col, 0, thickness);
}
