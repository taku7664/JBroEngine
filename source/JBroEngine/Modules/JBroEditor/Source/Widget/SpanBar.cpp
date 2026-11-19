#include <JBro/Editor/Widget/SpanBar.h>

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    Bool SpanBar(const char* id, UInt32 first, UInt32 last, UInt32 count, const ImVec4& color, UInt32 marker, Bool strong, Float height)
    {
        const Float width = ImGui::GetContentRegionAvail().x;
        const Float barHeight = height > 0.0f ? height : Float(ImGui::GetTextLineHeight());
        const ImVec2 size(width > 1.0f ? width : Float(1.0f), barHeight);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        const Bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        const Float rounding = ImGui::GetStyle().FrameRounding;
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        if (count == 0)
        {
            return hovered;
        }
        const Float cell = size.x / static_cast<Float>(count.Get());
        if (first <= last && first < count)
        {
            const UInt32 clampedLast = last < count ? last : count - 1;
            const ImVec2 from(origin.x + cell * static_cast<Float>(first.Get()), origin.y + 1.0f);
            const ImVec2 to(origin.x + cell * static_cast<Float>((clampedLast + 1).Get()), end.y - 1.0f);
            ImVec4 fill = color;
            fill.w = strong ? 1.0f : 0.55f;
            draw->AddRectFilled(from, to, ImGui::GetColorU32(fill), rounding);
        }
        if (marker < count)
        {
            // 칸의 가운데에 긋는다 - 칸이 좁아도 선이 이웃 칸으로 넘어 보이지 않는다.
            const Float x = origin.x + cell * (static_cast<Float>(marker.Get()) + 0.5f);
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        }
        return hovered;
    }
}
