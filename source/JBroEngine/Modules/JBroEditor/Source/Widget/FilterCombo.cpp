#include <JBro/Editor/Widget/FilterCombo.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

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
    }

    bool MatchesFilter(const char* text, const char* filter)
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
            bool matched = true;
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

    FilterCombo::FilterCombo(const char* id, ArrayView<const char* const> items, int& currentIndex)
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

    FilterCombo& FilterCombo::ShowFilter(bool show)
    {
        m_showFilter = show;
        return *this;
    }

    FilterCombo& FilterCombo::Width(float width)
    {
        m_width = width;
        return *this;
    }

    FilterCombo& FilterCombo::MaxVisibleItems(int count)
    {
        m_maxVisibleItems = count;
        return *this;
    }

    bool FilterCombo::Draw() const
    {
        const int itemCount = static_cast<int>(m_items.Size());
        const bool hasCurrent = m_currentIndex >= 0 && m_currentIndex < itemCount
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
            float widest = 0.0f;
            for (const char* item : m_items)
            {
                if (item != nullptr)
                {
                    widest = std::max(widest, ImGui::CalcTextSize(item).x);
                }
            }
            storage->SetFloat(widestKey, widest);
            storage->SetInt(countKey, itemCount);
        }
        float popupWidth = std::max(ImGui::CalcTextSize(preview).x, storage->GetFloat(widestKey, 0.0f));
        popupWidth = std::max(
            popupWidth + style.FramePadding.x * 4.0f + style.ScrollbarSize,
            ImGui::CalcItemWidth());
        const int maxVisible = std::clamp(m_maxVisibleItems, 1, DefaultMaxVisibleItems);
        const float popupMaxHeight = (m_showFilter ? ImGui::GetFrameHeightWithSpacing() : 0.0f)
            + ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(maxVisible)
            + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(popupWidth, 0.0f), ImVec2(FLT_MAX, popupMaxHeight));

        if (false == ImGui::BeginCombo(id, preview))
        {
            return false;
        }

        bool enterPressed = false;
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

        int chosen = -1;
        int firstVisible = -1;
        for (int index = 0; index < itemCount; ++index)
        {
            const char* item = m_items[static_cast<std::size_t>(index)];
            if (item == nullptr || false == MatchesFilter(item, g_filter))
            {
                continue;
            }
            if (firstVisible < 0)
            {
                firstVisible = index;
            }
            ImGui::PushID(index);
            const bool selected = index == m_currentIndex;
            if (ImGui::Selectable(item, selected))
            {
                chosen = index;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        if (firstVisible < 0)
        {
            ImGui::TextDisabled("%s",
                m_noItemsText != nullptr
                    ? m_noItemsText
                    : Loc::TextOr(LocKeys::CommonNoMatches, "No matches"));
        }
        if (chosen < 0 && enterPressed && firstVisible >= 0)
        {
            chosen = firstVisible;
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

    bool FilterCombo::operator()() const
    {
        return Draw();
    }
}
