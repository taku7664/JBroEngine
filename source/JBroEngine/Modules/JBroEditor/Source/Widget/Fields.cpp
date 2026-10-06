#include <JBro/Editor/Widget/Fields.h>

#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
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

    SearchBox& SearchBox::Width(Float width)
    {
        m_width = width;
        return *this;
    }

    SearchBox& SearchBox::ShowClear(Bool show)
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

    Bool SearchBox::Draw() const
    {
        Bool changed = false;
        ImGui::PushID(m_id != nullptr ? m_id : "##search_box");

        const Bool drawClear = m_showClear && m_text.size() > 0;
        const Float clearWidth = drawClear ? ImGui::GetFrameHeight() : 0.0f;
        const Float full =
            m_width != 0.0f ? m_width : Float(ImGui::GetContentRegionAvail().x);
        const Float fieldWidth = drawClear
            ? JBro::Max(1.0f, full - clearWidth - ImGui::GetStyle().ItemSpacing.x)
            : JBro::Max(1.0f, full);

        char buffer[SearchCapacity] = {};
        const std::size_t copied =
            m_text.size() < sizeof(buffer) - 1 ? m_text.size() : sizeof(buffer) - 1;
        std::memcpy(buffer, m_text.c_str(), copied);

        // **칸 안 왼쪽에 돋보기가 선다**(D-278). 글자가 그 뒤에서 시작하도록 칸의 가로 여백을 아이콘 폭만큼 늘린다.
        const ImVec2 padding = ImGui::GetStyle().FramePadding;
        const Float iconSpace = ImGui::GetTextLineHeight();
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x + iconSpace, padding.y));
        const Bool edited = ImGui::InputTextWithHint("##input",
            m_hint != nullptr ? m_hint : Loc::TextOr(LocKeys::CommonSearch, "Search"),
            buffer, sizeof(buffer), m_flags);
        ImGui::PopStyleVar();
        {
            const ImVec2 fieldMin = ImGui::GetItemRectMin();
            const ImVec2 fieldMax = ImGui::GetItemRectMax();
            const Float iconLeft = fieldMin.x + padding.x * 0.5f;
            DrawGlyphCentered(Icons::Search, ImVec2(iconLeft, fieldMin.y), ImVec2(iconLeft + iconSpace, fieldMax.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
        if (edited)
        {
            m_text = buffer;
            changed = true;
        }

        if (drawClear)
        {
            ImGui::SameLine();
            if (IconButton("##clear", Icons::Xmark)
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

    Bool SearchBox::operator()() const
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

    StatusBadge& StatusBadge::MinWidth(Float width)
    {
        m_minWidth = width;
        return *this;
    }

    void StatusBadge::Draw() const
    {
        const char* text = m_text != nullptr ? m_text : "";
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        // 글자 앞에 단계 아이콘이 선다(D-278). 글줄 높이의 정사각형과 그 뒤 여백만큼 넓다.
        const Float iconWidth = textSize.y + style.ItemInnerSpacing.x;
        const ImVec2 size(
            std::max(m_minWidth, iconWidth + textSize.x + style.FramePadding.x * 2.0f),
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
        const ImVec2 iconMin(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y);
        DrawGlyphCentered(SeverityIcon(m_severity), iconMin, ImVec2(iconMin.x + textSize.y, iconMin.y + textSize.y),
            ImGui::GetColorU32(base));
        drawList->AddText(
            ImVec2(iconMin.x + iconWidth, pos.y + style.FramePadding.y),
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

    IconButton& IconButton::Selected(Bool selected)
    {
        m_selected = selected;
        return *this;
    }

    IconButton& IconButton::Disabled(Bool disabled)
    {
        m_disabled = disabled;
        return *this;
    }

    IconButton& IconButton::Caption(const char* text)
    {
        m_caption = text;
        return *this;
    }

    Bool IconButton::Draw() const
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        StyleScope style;
        if (m_selected)
        {
            style.PushColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            style.PushColor(ImGuiCol_ButtonHovered,
                ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            style.PushColor(ImGuiCol_ButtonActive,
                ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        // 아이콘 칸은 줄 높이의 정사각형이다. 글자가 붙으면 그 뒤로 글자 폭과 여백만큼 늘린다.
        const Float square = ImGui::GetFrameHeight();
        const Bool hasCaption = m_caption != nullptr && *m_caption != '\0';
        const Float captionWidth = hasCaption
            ? ImGui::CalcTextSize(m_caption).x + ImGui::GetStyle().FramePadding.x
            : 0.0f;
        ImVec2 size = m_size;
        if (size.x <= 0.0f)
        {
            size.x = square + captionWidth;
        }
        if (size.y <= 0.0f)
        {
            size.y = square;
        }
        Bool clicked = false;
        {
            DisableScope disable(m_disabled);
            // 이름 없이 단추를 그리고 그 위에 아이콘을 얹는다. ImGui 가 이름을 그리면 줄 상자로 맞춰 처진다(D-277).
            clicked = ImGui::Button(m_id != nullptr ? m_id : "##icon_button", size);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            const Float iconRight = hasCaption ? min.x + square : Float(max.x);
            if (m_icon != nullptr)
            {
                DrawGlyphCentered(m_icon, min, ImVec2(iconRight, max.y), color);
            }
            if (hasCaption)
            {
                const Float textY = min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::GetWindowDrawList()->AddText(ImVec2(iconRight, textY), color, m_caption);
            }
        }
        style.Pop();
        Internal::ReportLastItem(target, m_selected, clicked, false == m_disabled);
        HoveredTooltip(m_tooltip);
        // 잠긴 버튼은 눌리지 않는다. `BeginDisabled` 가 이미 막지만, 돌려주는
        // 값에서도 막아 두어야 부르는 쪽이 조건을 두 번 쓰지 않는다.
        return clicked && false == m_disabled;
    }

    Bool IconButton::operator()() const
    {
        return Draw();
    }

    Bool Checkbox(const char* id, Bool& value)
    {
        bool raw = value;
        const bool changed = ImGui::Checkbox(id != nullptr ? id : "##check", &raw);
        value = raw;
        return changed;
    }

    Bool LayerMaskField(const char* id, ArrayView<const char* const> names, UInt32& mask)
    {
        const auto nameOf = [&](UInt32 bit, char* buffer, std::size_t capacity) -> const char* {
            if (bit < names.Size() && names[bit] != nullptr && names[bit][0] != '\0')
            {
                return names[bit];
            }
            std::snprintf(buffer, capacity, "#%u", static_cast<unsigned>(bit));
            return buffer;
        };
        char preview[160] = {};
        if (mask == 0u)
        {
            std::snprintf(preview, sizeof(preview), "%s", Loc::TextOr(LocKeys::InspectorLayersNothing, "Nothing"));
        }
        else if (mask == 0xFFFFFFFFu)
        {
            std::snprintf(preview, sizeof(preview), "%s", Loc::TextOr(LocKeys::InspectorLayersEverything, "Everything"));
        }
        else
        {
            std::size_t used = 0;
            for (UInt32 bit = 0; bit < 32 && used + 1 < sizeof(preview); ++bit)
            {
                if ((mask & (1u << bit)) == 0u)
                {
                    continue;
                }
                char number[8];
                const Int32 wrote = std::snprintf(preview + used, sizeof(preview) - used, used == 0 ? "%s" : ", %s",
                    nameOf(bit, number, sizeof(number)));
                if (wrote < 0)
                {
                    break;
                }
                used = std::min(sizeof(preview) - 1, used + static_cast<std::size_t>(wrote));
            }
        }

        Bool changed = false;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(id != nullptr ? id : "##layers", preview))
        {
            if (ImGui::Selectable(Loc::TextOr(LocKeys::InspectorLayersEverything, "Everything"), false,
                    ImGuiSelectableFlags_DontClosePopups))
            {
                changed = mask != 0xFFFFFFFFu;
                mask = 0xFFFFFFFFu;
            }
            if (ImGui::Selectable(Loc::TextOr(LocKeys::InspectorLayersNothing, "Nothing"), false,
                    ImGuiSelectableFlags_DontClosePopups))
            {
                changed = changed || mask != 0u;
                mask = 0u;
            }
            ImGui::Separator();
            for (UInt32 bit = 0; bit < 32; ++bit)
            {
                const Bool named = bit < names.Size() && names[bit] != nullptr && names[bit][0] != '\0';
                Bool on = (mask & (1u << bit)) != 0u;
                // 이름 없는 레이어는 켜져 있을 때만 보인다 - 서른두 줄을 늘 늘어놓지 않는다.
                if (false == named && false == on)
                {
                    continue;
                }
                char number[8];
                ImGui::PushID(static_cast<int>(bit));
                bool rawOn = on;
                const bool toggled = ImGui::Checkbox(nameOf(bit, number, sizeof(number)), &rawOn);
                on = rawOn;
                if (toggled)
                {
                    mask = on ? (mask | (1u << bit)) : (mask & ~(1u << bit));
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    Bool ObjectField(const char* id, ArrayView<const char* const> names, Int32& chosen, DragKind dropKind,
        UInt64& dropped)
    {
        dropped = 0;
        Bool changed = FilterCombo(id != nullptr ? id : "##object", names, chosen)
            .EmptyText(Loc::TextOr(LocKeys::InspectorObjectMissing, "Missing object"))
            .ShowFilter(true)
            .Draw();
        if (BeginDropTarget())
        {
            if (AcceptDropValue(dropKind, dropped))
            {
                changed = true;
            }
            EndDropTarget();
        }
        return changed;
    }

    Bool ColorField(const char* id, Float rgba[4])
    {
        // ImGui 는 float[4] 를 받는다. 떠서 넘기고 바뀌었을 때만 되돌려 쓴다.
        float raw[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
        const bool changed = ImGui::ColorEdit4(id != nullptr ? id : "##color", raw,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        if (changed)
        {
            for (int channel = 0; channel < 4; ++channel)
            {
                rgba[channel] = raw[channel];
            }
        }
        return changed;
    }

    Bool ScalarRunField(const char* id, Float* values, Int32 count, Float speed,
        Bool hasRange, Float rangeMin, Float rangeMax)
    {
        const char* label = id != nullptr ? id : "##run";
        if (hasRange)
        {
            return ImGui::SliderScalarN(label, ImGuiDataType_Float, values, count,
                &rangeMin, &rangeMax);
        }
        return ImGui::DragScalarN(label, ImGuiDataType_Float, values, count, speed);
    }

    Bool Splitter(
        const char* id, Bool vertical, Float thickness, Float* size,
        Float minSize, Float maxSize)
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

        const Bool hovered = ImGui::IsItemHovered();
        const Bool active = ImGui::IsItemActive();
        if (hovered || active)
        {
            ImGui::SetMouseCursor(vertical
                ? ImGuiMouseCursor_ResizeEW
                : ImGuiMouseCursor_ResizeNS);
        }

        Bool changed = false;
        if (active)
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            const Float moved = vertical ? delta.x : delta.y;
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

    Bool NameListEdit(const char* id, String& buffer, Float lines)
    {
        // **글자 칸이 이미 하는 일이다.** `String` 과 ImGui 의 `char` 버퍼 사이를 옮겨
        // 담는 것도, 여러 줄로 서는 것도, 아무도 치지 않았을 때 값을 그대로 두는 것도
        // `TextField` 안에 있다. 여기서 같은 것을 한 벌 더 쓰면 고칠 자리가 둘이 된다.
        return TextField(id, buffer).Multiline(true, lines).Draw();
    }

    void SplitLines(const String& buffer, Array<String>& out)
    {
        out.Clear();
        std::size_t start = 0;
        while (start <= buffer.size())
        {
            std::size_t stop = start;
            while (stop < buffer.size() && buffer[stop] != '\n')
            {
                ++stop;
            }
            // 윈도우에서 온 글자는 줄 끝에 `\r` 이 남는다. 그것까지 이름에 들어가면
            // 패턴이 영영 맞지 않는다.
            std::size_t end = stop;
            while (end > start && buffer[end - 1] == '\r')
            {
                --end;
            }
            if (end > start)
            {
                out.Add(String(buffer.c_str() + start, end - start));
            }
            if (stop >= buffer.size())
            {
                break;
            }
            start = stop + 1;
        }
    }

    void JoinLines(const Array<String>& items, String& buffer)
    {
        buffer.clear();
        for (std::size_t index = 0; index < items.Size(); ++index)
        {
            buffer.append(items[index].c_str(), items[index].size());
            buffer.append("\n", 1);
        }
    }
}
