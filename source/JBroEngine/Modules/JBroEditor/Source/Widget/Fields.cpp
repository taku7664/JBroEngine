#include <JBro/Editor/Widget/Fields.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cstring>

namespace JBro::Widget
{
    namespace
    {
        // ImGui 의 글자 칸은 `char` 버퍼를 받는다. 우리 `String` 과 주고받으려면
        // 한 번 옮겨 담아야 한다 - 콜백으로 늘려 받는 길도 있지만, 찾기 칸에
        // 넣을 글자는 짧다.
        constexpr std::size_t SearchCapacity = 256;
    }

    SearchBox::SearchBox(const char* id, String& text)
        : m_id(id)
        , m_text(text)
    {
    }

    SearchBox& SearchBox::Hint(const char* text)
    {
        m_hint = text;
        return *this;
    }

    SearchBox& SearchBox::Width(float width)
    {
        m_width = width;
        return *this;
    }

    SearchBox& SearchBox::ShowClear(bool show)
    {
        m_showClear = show;
        return *this;
    }

    SearchBox& SearchBox::ClearTooltip(const char* text)
    {
        m_clearTooltip = text;
        return *this;
    }

    SearchBox& SearchBox::Flags(ImGuiInputTextFlags flags)
    {
        m_flags = flags;
        return *this;
    }

    bool SearchBox::Draw() const
    {
        bool changed = false;
        ImGui::PushID(m_id != nullptr ? m_id : "##search_box");

        const bool drawClear = m_showClear && m_text.size() > 0;
        const float clearWidth = drawClear ? ImGui::GetFrameHeight() : 0.0f;
        const float full =
            m_width != 0.0f ? m_width : ImGui::GetContentRegionAvail().x;
        const float fieldWidth = drawClear
            ? std::max(1.0f, full - clearWidth - ImGui::GetStyle().ItemSpacing.x)
            : std::max(1.0f, full);

        char buffer[SearchCapacity] = {};
        const std::size_t copied =
            m_text.size() < sizeof(buffer) - 1 ? m_text.size() : sizeof(buffer) - 1;
        std::memcpy(buffer, m_text.c_str(), copied);

        ImGui::SetNextItemWidth(fieldWidth);
        if (ImGui::InputTextWithHint("##input",
            m_hint != nullptr ? m_hint : Loc::TextOr(LocKeys::CommonSearch, "Search"),
            buffer, sizeof(buffer), m_flags))
        {
            m_text = buffer;
            changed = true;
        }

        if (drawClear)
        {
            ImGui::SameLine();
            if (IconButton("clear", "x")
                .Size(ImVec2(clearWidth, 0.0f))
                .Tooltip(m_clearTooltip != nullptr
                    ? m_clearTooltip
                    : Loc::TextOr(LocKeys::CommonClear, "Clear"))
                .Draw())
            {
                m_text = "";
                changed = true;
            }
        }

        ImGui::PopID();
        return changed;
    }

    bool SearchBox::operator()() const
    {
        return Draw();
    }

    StatusBadge::StatusBadge(const char* text)
        : m_text(text)
    {
    }

    StatusBadge& StatusBadge::Level(Severity severity)
    {
        m_severity = severity;
        return *this;
    }

    StatusBadge& StatusBadge::Tooltip(const char* text)
    {
        m_tooltip = text;
        return *this;
    }

    StatusBadge& StatusBadge::MinWidth(float width)
    {
        m_minWidth = width;
        return *this;
    }

    void StatusBadge::Draw() const
    {
        const char* text = m_text != nullptr ? m_text : "";
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 size(
            std::max(m_minWidth, textSize.x + style.FramePadding.x * 2.0f),
            textSize.y + style.FramePadding.y * 2.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGui::PushID(this);
        ImGui::InvisibleButton("##status_badge", size);

        const ImVec4 base = SeverityColor(m_severity);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::GetColorU32(WithAlpha(base, 0.18f)), style.FrameRounding);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::GetColorU32(WithAlpha(base, 0.65f)), style.FrameRounding);
        drawList->AddText(
            ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y),
            ImGui::GetColorU32(base), text);

        HoveredTooltip(m_tooltip);
        ImGui::PopID();
    }

    void StatusBadge::operator()() const
    {
        Draw();
    }

    IconButton::IconButton(const char* id, const char* icon)
        : m_id(id)
        , m_icon(icon)
    {
    }

    IconButton& IconButton::Tooltip(const char* text)
    {
        m_tooltip = text;
        return *this;
    }

    IconButton& IconButton::Size(ImVec2 size)
    {
        m_size = size;
        return *this;
    }

    IconButton& IconButton::Selected(bool selected)
    {
        m_selected = selected;
        return *this;
    }

    IconButton& IconButton::Disabled(bool disabled)
    {
        m_disabled = disabled;
        return *this;
    }

    bool IconButton::Draw() const
    {
        ImGui::PushID(m_id != nullptr ? m_id : "");
        StyleScope style;
        if (m_selected)
        {
            style.PushColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            style.PushColor(ImGuiCol_ButtonHovered,
                ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            style.PushColor(ImGuiCol_ButtonActive,
                ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        bool clicked = false;
        {
            DisableScope disable(m_disabled);
            clicked = ImGui::Button(m_icon != nullptr ? m_icon : "", m_size);
        }
        style.Pop();
        HoveredTooltip(m_tooltip);
        ImGui::PopID();
        // 잠긴 버튼은 눌리지 않는다. `BeginDisabled` 가 이미 막지만, 돌려주는
        // 값에서도 막아 두어야 부르는 쪽이 조건을 두 번 쓰지 않는다.
        return clicked && false == m_disabled;
    }

    bool IconButton::operator()() const
    {
        return Draw();
    }

    bool Splitter(
        const char* id, bool vertical, float thickness, float* size,
        float minSize, float maxSize)
    {
        if (size == nullptr)
        {
            return false;
        }
        ImGui::PushID(id);
        const ImVec2 handleSize = vertical
            ? ImVec2(thickness, ImGui::GetContentRegionAvail().y)
            : ImVec2(ImGui::GetContentRegionAvail().x, thickness);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##splitter", handleSize);

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
        {
            ImGui::SetMouseCursor(vertical
                ? ImGuiMouseCursor_ResizeEW
                : ImGuiMouseCursor_ResizeNS);
        }

        bool changed = false;
        if (active)
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            const float moved = vertical ? delta.x : delta.y;
            if (moved != 0.0f)
            {
                *size = std::clamp(*size + moved, minSize, maxSize);
                changed = true;
            }
        }

        // 잡을 수 있다는 것이 보여야 한다. 아무 표시도 없으면 두 칸이 그냥
        // 붙어 있는 것으로 보인다.
        const ImU32 color = ImGui::GetColorU32(active
            ? ImGuiCol_SeparatorActive
            : (hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2(pos.x + handleSize.x, pos.y + handleSize.y), color);

        ImGui::PopID();
        return changed;
    }
}
