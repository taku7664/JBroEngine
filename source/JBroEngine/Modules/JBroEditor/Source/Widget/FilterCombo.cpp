#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/GuideFocus.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    namespace
    {
        // 검색 글은 팝업이 열려 있는 동안만 산다. 드롭다운 팝업은 한 번에 하나만 열리므로
        // 버퍼 하나면 되고, 열릴 때마다 비운다(`IsWindowAppearing`).
        constexpr std::size_t FilterCapacity = 128;
        char g_filter[FilterCapacity] = {};

        char Lower(char value)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        }

        // 갈래 이름을 견준다. 같은 글자면 같은 갈래다 - 부르는 쪽이 같은 포인터를
        // 넘긴다는 보장이 없고(로컬라이징을 거치면 매 프레임 다른 버퍼일 수 있다),
        // 포인터로 견주면 갈래마다 제목줄이 겹쳐 뜬다.
        Bool SameGroup(const char* left, const char* right)
        {
            if (left == right)
            {
                return true;
            }
            if (left == nullptr || right == nullptr)
            {
                return false;
            }
            return std::strcmp(left, right) == 0;
        }
    }

    Bool MatchesFilter(const char* text, const char* filter)
    {
        if (filter == nullptr || filter[0] == '\0')
        {
            return true;
        }
        if (text == nullptr)
        {
            return false;
        }
        const std::size_t textLength = std::strlen(text);
        const std::size_t filterLength = std::strlen(filter);
        if (filterLength > textLength)
        {
            return false;
        }
        for (std::size_t start = 0; start + filterLength <= textLength; ++start)
        {
            Bool matched = true;
            for (std::size_t at = 0; at < filterLength; ++at)
            {
                if (Lower(text[start + at]) != Lower(filter[at]))
                {
                    matched = false;
                    break;
                }
            }
            if (matched)
            {
                return true;
            }
        }
        return false;
    }

    FilterCombo::FilterCombo(const char* id, ArrayView<const char* const> items, Int32& currentIndex)
        : m_id(id)
        , m_items(items)
        , m_currentIndex(currentIndex)
    {
    }

    FilterCombo& FilterCombo::EmptyText(const char* text)
    {
        m_emptyText = text;
        return *this;
    }

    FilterCombo& FilterCombo::FilterHint(const char* text)
    {
        m_filterHint = text;
        return *this;
    }

    FilterCombo& FilterCombo::NoItemsText(const char* text)
    {
        m_noItemsText = text;
        return *this;
    }

    FilterCombo& FilterCombo::ShowFilter(Bool show)
    {
        m_showFilter = show;
        return *this;
    }

    FilterCombo& FilterCombo::ItemGroups(ArrayView<const char* const> groups)
    {
        m_groups = groups;
        return *this;
    }

    FilterCombo& FilterCombo::ItemEnabled(ArrayView<const Bool> enabled)
    {
        m_enabled = enabled;
        return *this;
    }

    FilterCombo& FilterCombo::DisabledTooltip(const char* text)
    {
        m_disabledTooltip = text;
        return *this;
    }

    FilterCombo& FilterCombo::ItemTargets(NameId name, ArrayView<const UInt64> keys)
    {
        m_targetName = name;
        m_targetKeys = keys;
        return *this;
    }

    FilterCombo& FilterCombo::Width(Float width)
    {
        m_width = width;
        return *this;
    }

    FilterCombo& FilterCombo::MaxVisibleItems(Int32 count)
    {
        m_maxVisibleItems = count;
        return *this;
    }

    Bool FilterCombo::Draw() const
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        const Int32 itemCount = static_cast<int>(m_items.Size());
        // 갈래는 항목과 길이가 맞을 때만 쓴다. 어긋난 배열을 읽으면 그 자리에서 죽는다.
        const Bool hasGroups = m_groups.Size() == m_items.Size() && m_items.Size() > 0;
        const Bool hasEnabled = m_enabled.Size() == m_items.Size();
        const Bool hasTargets = m_targetName != InvalidNameId && m_targetKeys.Size() == m_items.Size();
        // **제목줄도 자리를 먹는다.** 항목 수만으로 팝업 높이를 잡으면 갈래가 붙는 만큼
        // 목록이 창 밖으로 흘러 마지막 갈래가 잘린다.
        Int32 groupCount = 0;
        if (hasGroups)
        {
            for (std::size_t at = 0; at < m_groups.Size(); ++at)
            {
                if (at == 0 || false == SameGroup(m_groups[at], m_groups[at - 1]))
                {
                    ++groupCount;
                }
            }
        }
        const Bool manyGroups = groupCount > 1;

        const Bool hasCurrent = m_currentIndex >= 0 && m_currentIndex < itemCount
            && m_items[static_cast<std::size_t>(m_currentIndex)] != nullptr;
        const char* preview = hasCurrent
            ? m_items[static_cast<std::size_t>(m_currentIndex)]
            : (m_emptyText != nullptr ? m_emptyText : "");

        const ImGuiStyle& style = ImGui::GetStyle();
        if (m_width != 0.0f)
        {
            ImGui::SetNextItemWidth(m_width);
        }
        // 트리거가 좁아도 팝업은 가장 긴 이름과 검색 칸이 잘리지 않는 폭을 갖는다. 트리거보다 좁아지지는 않는다.
        // **가장 긴 이름은 항목 수가 바뀔 때만 잰다.** 팝업이 닫혀 있는 프레임에도 이 함수는 불리는데, 에셋 수천
        // 개를 칸마다 프레임마다 재면 그것이 인스펙터의 비용이 된다. 창의 상태 저장소에 둔다.
        const char* id = m_id != nullptr ? m_id : "##filter_combo";
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID comboId = ImGui::GetID(id);
        const ImGuiID widestKey = ImHashStr("##widest", 0, comboId);
        const ImGuiID countKey = ImHashStr("##count", 0, comboId);
        if (storage->GetInt(countKey, -1) != itemCount)
        {
            Float widest = 0.0f;
            for (const char* item : m_items)
            {
                if (item != nullptr)
                {
                    widest = JBro::Max(widest, ImGui::CalcTextSize(item).x);
                }
            }
            storage->SetFloat(widestKey, widest);
            storage->SetInt(countKey, itemCount);
        }
        Float popupWidth = std::max(ImGui::CalcTextSize(preview).x, storage->GetFloat(widestKey, 0.0f));
        popupWidth = JBro::Max(
            popupWidth + style.FramePadding.x * 4.0f + style.ScrollbarSize,
            ImGui::CalcItemWidth());
        const Int32 maxVisible = JBro::Clamp(m_maxVisibleItems, 1, DefaultMaxVisibleItems);
        // 보이는 줄은 항목 여덟에 **갈래 제목줄까지**다. 제목줄을 빼고 재면 갈래가 붙는
        // 만큼 팝업이 예산을 넘어, 창 아래에 열렸을 때 마지막 갈래가 화면 밖으로 나간다.
        const Int32 headingLines = manyGroups ? groupCount : Int32(0);
        const Float popupMaxHeight = (m_showFilter ? ImGui::GetFrameHeightWithSpacing() : 0.0f)
            + ImGui::GetTextLineHeightWithSpacing()
                * static_cast<float>(maxVisible + headingLines)
            + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(popupWidth, 0.0f), ImVec2(FLT_MAX, popupMaxHeight));

        // **트리거는 열기 전에 알린다.** 열린 뒤에는 지금 창이 팝업이라 트리거의 자리를 잴 수 없다. 열림은 목록이 떠 있는가다 -
        // 가이드가 목록의 항목까지 가리키면(반례 ④) 목록이 뜬 것을 보고 한 칸 들어간다. 팝업 이름은 `BeginCombo` 가 짓는 것과 같다.
        const ImVec2 frameMin = ImGui::GetCursorScreenPos();
        const Bool listOpen = ImGui::IsPopupOpen(ImHashStr("##ComboPopup", 0, comboId), ImGuiPopupFlags_None);
        ReportGuideTarget(target, frameMin,
            ImVec2(frameMin.x + ImGui::CalcItemWidth(), frameMin.y + ImGui::GetFrameHeight()), listOpen, false);
        if (false == ImGui::BeginCombo(id, preview))
        {
            return false;
        }

        Bool enterPressed = false;
        if (m_showFilter)
        {
            if (ImGui::IsWindowAppearing())
            {
                g_filter[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter",
                m_filterHint != nullptr
                    ? m_filterHint
                    : Loc::TextOr(LocKeys::CommonSearch, "Search"),
                g_filter, sizeof(g_filter));
            // Enter 는 글자 칸을 비활성으로 만든다. 그 프레임에 Enter 가 눌려 있었으면
            // 보이는 첫 항목을 고른다. Escape·다른 곳 누르기로 나간 것과 구분한다.
            enterPressed = ImGui::IsItemDeactivated()
                && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
            ImGui::Separator();
        }
        else if (ImGui::IsWindowAppearing())
        {
            g_filter[0] = '\0';
        }

        Int32 chosen = -1;
        Int32 firstEnabled = -1;
        Bool drewAny = false;
        const char* drawnGroup = nullptr;
        for (Int32 index = 0; index < itemCount; ++index)
        {
            const std::size_t at = static_cast<std::size_t>(index);
            const char* item = m_items[at];
            if (item == nullptr || false == MatchesFilter(item, g_filter))
            {
                continue;
            }
            // 제목줄은 **보이는** 첫 항목 바로 앞에 넣는다. 그래야 걸러내기로 항목이
            // 하나도 남지 않은 갈래의 제목만 덩그러니 남는 일이 없다.
            if (manyGroups && (false == drewAny || false == SameGroup(m_groups[at], drawnGroup)))
            {
                ImGui::SeparatorText(m_groups[at] != nullptr ? m_groups[at] : "");
                drawnGroup = m_groups[at];
            }
            drewAny = true;
            const Bool enabled = false == hasEnabled || m_enabled[at];
            if (enabled && firstEnabled < 0)
            {
                firstEnabled = index;
            }
            ImGui::PushID(index);
            const Bool selected = index == m_currentIndex;
            ImGui::BeginDisabled(false == enabled);
            const Bool pressed = ImGui::Selectable(item, selected) && enabled;
            if (pressed)
            {
                chosen = index;
            }
            ImGui::EndDisabled();
            if (hasTargets)
            {
                // 회색 항목도 알린다 - 가이드가 가리킨 항목이 이미 붙어 있으면 왜 안 눌리는지 말풍선이 적는다(반례 ⑥).
                Internal::ReportLastItem(GuideFocusTarget{ m_targetName, m_targetKeys[at] }, false, pressed, enabled,
                    m_disabledTooltip);
            }
            if (false == enabled)
            {
                HoveredTooltip(m_disabledTooltip, ImGuiHoveredFlags_AllowWhenDisabled);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        if (false == drewAny)
        {
            ImGui::TextDisabled("%s",
                m_noItemsText != nullptr
                    ? m_noItemsText
                    : Loc::TextOr(LocKeys::CommonNoMatches, "No matches"));
        }
        // Enter 는 **고를 수 있는** 첫 항목을 고른다. 회색 항목이 맨 위에 있다고 해서
        // Enter 가 아무 일도 하지 않으면, 왜 안 되는지 알 수 없다.
        if (chosen < 0 && enterPressed && firstEnabled >= 0)
        {
            chosen = firstEnabled;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();

        if (chosen < 0 || chosen == m_currentIndex)
        {
            return false;
        }
        m_currentIndex = chosen;
        return true;
    }

    Bool FilterCombo::operator()() const
    {
        return Draw();
    }
}
