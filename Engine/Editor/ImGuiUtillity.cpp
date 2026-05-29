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

void ImGui::Utillity::LoadingSpinner(float radius, ImVec4 color)
{
	if (radius <= 0.0f)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		float paddingY = style.FramePadding.y;
		radius = ImGui::GetFrameHeight() * 0.5f - paddingY;
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
