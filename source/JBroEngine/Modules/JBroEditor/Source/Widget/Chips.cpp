#include <JBro/Editor/Widget/Chips.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>

namespace JBro::Widget
{
    Bool ChoiceChips(const char* id, ArrayView<const char* const> names, Int32& selected)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushID(id);
        Bool changed = false;
        const Float gap = style.ItemSpacing.x * 0.75f;
        const Float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const Float height = ImGui::GetFrameHeight();
        const Float padding = style.FramePadding.x * 1.6f;
        Float lastRight = 0.0f;
        for (std::size_t index = 0; index < names.Size(); ++index)
        {
            const char* name = names.Data()[index] != nullptr ? names.Data()[index] : "?";
            const ImVec2 textSize = ImGui::CalcTextSize(name);
            const ImVec2 chip(textSize.x + padding * 2.0f, height);
            // 같은 줄에 들어가면 잇고, 넘치면 다음 줄에서 시작한다.
            if (index > 0 && lastRight + gap + chip.x <= right)
            {
                ImGui::SameLine(0.0f, gap);
            }
            const Int32 number = static_cast<Int32>(static_cast<int>(index));
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const Bool pressed = HitArea("##chip", chip);
            const Bool hovered = ImGui::IsItemHovered();
            const ImVec2 max(min.x + chip.x, min.y + chip.y);
            lastRight = max.x;
            const Bool isSelected = selected == number;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const Float rounding = chip.y * 0.5f;
            if (isSelected)
            {
                draw->AddRectFilled(min, max, ImGui::GetColorU32(EditorTheme::Accent), rounding);
            }
            else
            {
                draw->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? EditorTheme::Hover : EditorTheme::Raised), rounding);
                draw->AddRect(min, max, ImGui::GetColorU32(EditorTheme::LineStrong), rounding);
            }
            const ImVec4 ink = isSelected || hovered ? EditorTheme::Ink : EditorTheme::InkMuted;
            draw->AddText(ImVec2(min.x + padding, min.y + (chip.y - textSize.y) * 0.5f), ImGui::GetColorU32(ink), name);
            if (pressed && false == isSelected)
            {
                selected = number;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        return changed;
    }
}
