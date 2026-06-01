#include "pch.h"
#include "ImGuiUtillity.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
namespace
{
	std::string ToUtf8PathString(const File::Path& path)
	{
		const auto text = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
	}
}
#endif

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

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
File::FileDialogOwnerHandle ImGui::Utillity::GetDialogOwnerHandle(File::FileDialogOwnerHandle owner)
{
	if (owner)
	{
		return owner;
	}
	if (ImGuiViewport* viewport = ImGui::GetMainViewport())
	{
		if (viewport->PlatformHandleRaw)
		{
			return viewport->PlatformHandleRaw;
		}
		return viewport->PlatformHandle;
	}
	return nullptr;
}

bool ImGui::Utillity::BrowseFolderButton(
	const char* id,
	std::string& inOutPath,
	const wchar_t* title,
	const wchar_t* initialDirectory,
	File::FileDialogOwnerHandle owner)
{
	if (false == ImGui::SmallButton(id))
	{
		return false;
	}

	File::Path selectedPath;
	if (false == File::ShowOpenFolderDialog(GetDialogOwnerHandle(owner), title, initialDirectory, selectedPath))
	{
		return false;
	}

	inOutPath = ToUtf8PathString(selectedPath);
	return true;
}

bool ImGui::Utillity::BrowseFileButton(
	const char* id,
	std::string& inOutPath,
	const wchar_t* title,
	const wchar_t* initialDirectory,
	std::vector<File::FileDialogFilter> filters,
	File::FileDialogOwnerHandle owner)
{
	if (false == ImGui::SmallButton(id))
	{
		return false;
	}

	File::Path selectedPath;
	if (false == File::ShowOpenFileDialog(GetDialogOwnerHandle(owner), title, initialDirectory, std::move(filters), selectedPath))
	{
		return false;
	}

	inOutPath = ToUtf8PathString(selectedPath);
	return true;
}

bool ImGui::Utillity::BrowseFilesButton(
	const char* id,
	std::vector<File::Path>& outPaths,
	const wchar_t* title,
	const wchar_t* initialDirectory,
	std::vector<File::FileDialogFilter> filters,
	File::FileDialogOwnerHandle owner)
{
	if (false == ImGui::SmallButton(id))
	{
		return false;
	}

	return File::ShowOpenFileDialog(GetDialogOwnerHandle(owner), title, initialDirectory, std::move(filters), outPaths);
}
#endif



void ImGui::Utillity::LoadingSpinner(float radius, ImVec4 color)
{
	LoadingSpinnerEx(radius, 2.0f, 5.0f, color);
}

void ImGui::Utillity::LoadingSpinnerEx(float radius, float thickness, float spinSpeed, ImVec4 color)
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec2 padding = style.FramePadding;

	if (radius <= 0.0f)
	{
		radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
	}

	const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
	const ImVec2 center = cursorScreen + padding + ImVec2(radius, radius);

	constexpr int   segments = 20;
	constexpr int   visibleSegments = segments - 4; // 꼬리 잘라서 회전 방향이 보이도록

	const float start = static_cast<float>(ImGui::GetTime()) * spinSpeed;
	const float angleOffset = 2.0f * IM_PI / static_cast<float>(segments);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 colU32 = ImGui::GetColorU32(color);

	for (int i = 0; i < visibleSegments; ++i)
	{
		const float angle = start + i * angleOffset;
		const ImVec2 p1(center.x + std::cos(angle) * radius,
			center.y + std::sin(angle) * radius);
		const ImVec2 p2(center.x + std::cos(angle + angleOffset) * radius,
			center.y + std::sin(angle + angleOffset) * radius);
		drawList->AddLine(p1, p2, colU32, thickness);
	}

	// 커서 전진 — 호출자가 SameLine 만 호출하면 다음 위젯이 자연스럽게 이어붙음
	ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f) + padding);
}

void ImGui::Utillity::CheckMark(float radius, ImVec4 color)
{
	ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 padding = style.FramePadding;

	if (radius <= 0.0f)
	{
		radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
	}

	const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
	const ImVec2 center = cursorScreen + padding + ImVec2(radius, radius);

	constexpr float kLineThickness = 2.0f;

	// 체크 마크의 세 꼭짓점 — 반지름 기준 상대 좌표 (왼쪽 → 아래 꺾임 → 오른쪽 위).
	const ImVec2 p1(center.x - radius * 0.55f, center.y + radius * 0.05f);
	const ImVec2 p2(center.x - radius * 0.15f, center.y + radius * 0.45f);
	const ImVec2 p3(center.x + radius * 0.60f, center.y - radius * 0.45f);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 colU32   = ImGui::GetColorU32(color);
	drawList->AddLine(p1, p2, colU32, kLineThickness);
	drawList->AddLine(p2, p3, colU32, kLineThickness);

	// 커서 전진 — LoadingSpinner 와 동일 규칙이라 두 위젯을 같은 줄에서 맞바꿔도 정렬 유지.
	ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f) + padding);
}
