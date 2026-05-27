#include "pch.h"
#include "ImGuiUtillity.h"

ImGui::Utillity::StyleBuilder::~StyleBuilder()
{
	PopStyle();
}

void ImGui::Utillity::StyleBuilder::PopStyle()
{
	if (m_pushStyleVarCount > 0)
	{
		ImGui::PopStyleVar(m_pushStyleVarCount);
		m_pushStyleVarCount = 0;
	}
	if (m_pushStyleColCount > 0)
	{
		ImGui::PopStyleColor(m_pushStyleColCount);
		m_pushStyleColCount = 0;
	}
}

ImGui::Utillity::DisableScope::DisableScope(bool disable)
	: m_disabled(disable)
{
	if (m_disabled)
	{
		ImGui::BeginDisabled();
	}
}

ImGui::Utillity::DisableScope::~DisableScope()
{
	if (m_disabled)
	{
		ImGui::EndDisabled();
	}
}

ImGui::Utillity::FormLayout::FormLayout(
	const char* id,
	float spacing,
	ImVec2 padding,
	float labelWidth)
	: m_spacing(spacing)
	, m_labelWidth(labelWidth)
{
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_SizingStretchProp |
		ImGuiTableFlags_NoSavedSettings |
		ImGuiTableFlags_NoBordersInBody |
		ImGuiTableFlags_NoPadOuterX;

	m_isOpen = ImGui::BeginTable(id, 2, tableFlags);

	if (m_isOpen)
	{
		m_styleBuilder.PushStyleVar(ImGuiStyleVar_CellPadding, padding);

		const ImGuiTableColumnFlags labelColumnFlags =
			ImGuiTableColumnFlags_WidthFixed;

		const ImGuiTableColumnFlags fieldColumnFlags =
			ImGuiTableColumnFlags_WidthStretch;

		ImGui::TableSetupColumn("Label", labelColumnFlags, m_labelWidth);
		ImGui::TableSetupColumn("Field", fieldColumnFlags);
	}
}

ImGui::Utillity::FormLayout::~FormLayout()
{
	if (m_isOpen)
	{
		ImGui::EndTable();
	}
}

bool ImGui::Utillity::FormLayout::IsOpen() const
{
	return m_isOpen;
}

ImGui::Utillity::IDGroup::IDGroup(const char* strId)
{
	ImGui::PushID(strId);
	m_hasId = true;
}

ImGui::Utillity::IDGroup::IDGroup(int intId)
{
	ImGui::PushID(intId);
	m_hasId = true;
}

ImGui::Utillity::IDGroup::IDGroup(const void* ptrId)
{
	ImGui::PushID(ptrId);
	m_hasId = true;
}

ImGui::Utillity::IDGroup::~IDGroup()
{
	if (m_hasId)
	{
		ImGui::PopID();
	}
}

void ImGui::Utillity::TextEx::Show(const char* text)
{
	ImGui::TextUnformatted(text);
	if (m_useTooltip)
	{
		HoveredToolTip(text, m_hoveredFlags);
	}
}

ImGui::Utillity::TextEx& ImGui::Utillity::TextEx::UseHoveredToolTip(
	bool use,
	ImGuiHoveredFlags flags)
{
	m_useTooltip = use;
	m_hoveredFlags = flags;
	return *this;
}

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

bool ImGui::Utillity::VerticalSplitter(
	const char* id,
	float& ratio,
	ImVec2 availSpace,
	const float minRatio,
	const float maxRatio,
	float thickness)
{
	ImGui::SameLine(0.0f, 0.0f);

	IDGroup idGroup(id);

	const float width = std::max(availSpace.x, 1.0f);

	const ImGuiID grabOffsetKey = ImGui::GetID("##VerticalSplitterGrabOffsetX");
	const ImGuiID containerMinKey = ImGui::GetID("##VerticalSplitterContainerMinX");
	const ImGuiID dragWidthKey = ImGui::GetID("##VerticalSplitterDragWidth");

	ImGuiStorage* storage = ImGui::GetStateStorage();

	{
		StyleBuilder styleBuilder;
		styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 1.0f, 0.3f));
		styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.6f, 1.0f, 0.5f));

		ImGui::Button("##InspSplitter", ImVec2(thickness, availSpace.y));
		ImGui::SameLine(0.0f, 0.0f);
	}

	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}

	if (ImGui::IsItemActivated())
	{
		const ImVec2 splitterMin = ImGui::GetItemRectMin();

		const float mouseX = ImGui::GetMousePos().x;

		// 스플리터 내부에서 어디를 잡았는지 저장
		const float grabOffsetX = mouseX - splitterMin.x;

		// 현재 ratio 기준으로 전체 컨테이너 시작 X를 역산
		const float containerMinX = splitterMin.x - ratio * width;

		storage->SetFloat(grabOffsetKey, grabOffsetX);
		storage->SetFloat(containerMinKey, containerMinX);
		storage->SetFloat(dragWidthKey, width);
	}

	if (ImGui::IsItemActive())
	{
		const float mouseX = ImGui::GetMousePos().x;

		const float grabOffsetX = storage->GetFloat(grabOffsetKey, 0.0f);
		const float containerMinX = storage->GetFloat(containerMinKey, 0.0f);
		const float dragWidth = std::max(storage->GetFloat(dragWidthKey, width), 1.0f);

		ratio = (mouseX - grabOffsetX - containerMinX) / dragWidth;
		ratio = std::clamp(ratio, minRatio, maxRatio);

		return true;
	}

	return false;
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

void ImGui::Utillity::LoadingSpinner(float radius, ImVec4 color)
{
	if (radius <= 0.0f)
	{
		radius = ImGui::GetFrameHeight() * 0.5f;
	}

	const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
	const ImVec2 center(cursorScreen.x + radius, cursorScreen.y + radius);

	constexpr int   segments         = 20;
	constexpr int   visibleSegments  = segments - 4; // 꼬리 잘라서 회전 방향이 보이도록
	constexpr float kSpinSpeed       = 3.0f;
	constexpr float kLineThickness   = 2.0f;

	const float start       = static_cast<float>(ImGui::GetTime()) * kSpinSpeed;
	const float angleOffset = 2.0f * IM_PI / static_cast<float>(segments);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 colU32   = ImGui::GetColorU32(color);

	for (int i = 0; i < visibleSegments; ++i)
	{
		const float angle = start + i * angleOffset;
		const ImVec2 p1(center.x + std::cos(angle)               * radius,
		                center.y + std::sin(angle)               * radius);
		const ImVec2 p2(center.x + std::cos(angle + angleOffset) * radius,
		                center.y + std::sin(angle + angleOffset) * radius);
		drawList->AddLine(p1, p2, colU32, kLineThickness);
	}

	// 커서 전진 — 호출자가 SameLine 만 호출하면 다음 위젯이 자연스럽게 이어붙음
	ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
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
