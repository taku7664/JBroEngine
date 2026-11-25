#include <JBro/Editor/Widget/Pane.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Types/Float.h>

#include <algorithm>

namespace JBro::Widget
{
    TrailingButtons::TrailingButtons()
        : m_min(ImGui::GetItemRectMin())
        , m_max(ImGui::GetItemRectMax())
    {
        m_right = m_max.x - ImGui::GetStyle().FramePadding.x * 0.5f;
    }

    Bool TrailingButtons::Icon(const char* id, const char* icon, const char* tooltip, Bool disabled)
    {
        // 앞 항목의 높이 안에 들어가는 정사각형이다. 머리가 단추보다 낮으면 머리 높이에 맞춘다.
        const Float rowHeight = m_max.y - m_min.y;
        const Float side = (std::min)(Float(ImGui::GetFrameHeight()), rowHeight);
        m_right -= side;
        // 같은 줄로 돌아가 단추를 놓는다. 단추가 마지막 항목이 되어도 다음 줄은 단추 밑에서 시작한다(머리와 같은 줄, 같은 높이).
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(m_right, m_min.y + (rowHeight - side) * 0.5f));
        const Bool pressed = IconButton(id, icon).Flat().Size(ImVec2(side, side)).Tooltip(tooltip).Disabled(disabled).Draw();
        m_right -= 2.0f;
        return pressed;
    }

    void RowTrailingText(const char* text, Bool muted)
    {
        if (text == nullptr || *text == '\0')
        {
            return;
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 size = ImGui::CalcTextSize(text);
        const Float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const ImVec4 color = muted ? EditorTheme::InkMuted : EditorTheme::Ink;
        ImGui::GetWindowDrawList()->AddText(ImVec2(right - size.x - ImGui::GetStyle().FramePadding.x, min.y + (max.y - min.y - size.y) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(color), text);
    }

    void DetailHeading(const char* icon, const char* title, const char* caption)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const Float width = (std::max)(Float(1.0f), Float(ImGui::GetContentRegionAvail().x));
        const Float line = ImGui::GetTextLineHeight();
        const Bool hasCaption = caption != nullptr && *caption != '\0';
        // 이름 줄과 설명 줄, 위아래 여백이다. 설명이 없으면 한 줄이다.
        const Float height = line * (hasCaption ? 2.0f : 1.0f) + style.FramePadding.y * 3.0f + (hasCaption ? 2.0f : 0.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const Float iconSide = line * 1.6f;
        // 아이콘은 이름과 설명 두 줄 높이의 가운데에 선다.
        if (icon != nullptr && *icon != '\0')
        {
            DrawGlyphCentered(icon, ImVec2(origin.x, origin.y), ImVec2(origin.x + iconSide, origin.y + height),
                ImGui::ColorConvertFloat4ToU32(EditorTheme::InkMuted), line * 1.25f);
        }
        const Float textLeft = origin.x + iconSide + style.ItemSpacing.x * 0.5f;
        const Float textTop = origin.y + style.FramePadding.y;
        draw->PushClipRect(ImVec2(textLeft, origin.y), ImVec2(origin.x + width, origin.y + height), true);
        draw->AddText(ImVec2(textLeft, textTop), ImGui::ColorConvertFloat4ToU32(EditorTheme::Ink), title != nullptr ? title : "");
        if (hasCaption)
        {
            draw->AddText(ImVec2(textLeft, textTop + line + 2.0f), ImGui::ColorConvertFloat4ToU32(EditorTheme::InkMuted), caption);
        }
        draw->PopClipRect();
        // 줄 전체를 한 항목으로 둔다 - 뒤의 `TrailingButtons` 가 이 칸을 본다. 단추가 위에 서도록 겹침을 허락한다.
        ImGui::SetNextItemAllowOverlap();
        HitArea("##detail_heading", ImVec2(width, height), ImGuiButtonFlags_None);
        // 아래 선은 줄 안의 맨 아래다 - 뒤에 항목을 더 두면 `TrailingButtons` 가 제목 줄을 못 본다.
        draw->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + width, origin.y + height - 1.0f),
            ImGui::ColorConvertFloat4ToU32(EditorTheme::Line));
    }

    void EmptyState(const char* icon, const char* text)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const Float width = (std::max)(Float(1.0f), Float(available.x));
        const Float height = (std::max)(Float(ImGui::GetTextLineHeight() * 4.0f), Float(available.y));
        const Float line = ImGui::GetTextLineHeight();
        const Float iconSize = line * 2.0f;
        const Float wrap = (std::max)(Float(40.0f), Float(width - ImGui::GetStyle().FramePadding.x * 4.0f));
        const ImVec2 textSize = ImGui::CalcTextSize(text != nullptr ? text : "", nullptr, false, wrap);
        const Float blockHeight = iconSize + line * 0.5f + textSize.y;
        const Float top = origin.y + (height - blockHeight) * 0.45f;
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::InkMuted);
        if (icon != nullptr && *icon != '\0')
        {
            DrawGlyphCentered(icon, ImVec2(origin.x, top), ImVec2(origin.x + width, top + iconSize), color, iconSize);
        }
        if (text != nullptr && *text != '\0')
        {
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(origin.x + (width - textSize.x) * 0.5f, top + iconSize + line * 0.5f), color, text, nullptr, wrap);
        }
        ImGui::Dummy(ImVec2(width, height));
    }
}
