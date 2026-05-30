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

bool ImGui::Utillity::IsWindowDrawable(ImGuiWindow* window)
{
    if (!window)
        window = ImGui::GetCurrentWindowRead();
    if (!window)
        return false;

    return !window->SkipItems;
}

bool ImGui::Utillity::HoveredToolTip(const char* toolTip , ImGuiHoveredFlags flags)
{
	bool isHovered = ImGui::IsItemHovered(flags);
	if (isHovered)
	{
		ImGui::SetTooltip(toolTip);
	}
	return isHovered;
}



void ImGui::Utillity::LoadingSpinner(float radius, ImVec4 color)
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec2 padding = style.FramePadding;

	if (radius <= 0.0f)
	{
		radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
	}

	const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
	const ImVec2 center = cursorScreen + padding + ImVec2(radius, radius);

	constexpr int   segments         = 20;
	constexpr int   visibleSegments  = segments - 4; // 꼬리 잘라서 회전 방향이 보이도록
	constexpr float kSpinSpeed       = 5.0f;
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
