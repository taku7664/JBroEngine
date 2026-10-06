#include <JBro/Editor/Widget/Waveform.h>
#include <JBro/Types/ValueMath.h>

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::Widget
{
    Bool Waveform(const char* id, ArrayView<const Float> peaks, Float progress, Float height, Float& seekFraction)
    {
        const Float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width > 1.0f ? width : Float(1.0f), height > 1.0f ? height : Float(1.0f));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const Bool pressed = HitArea(id, size, ImGuiButtonFlags_MouseButtonLeft);
        const Bool held = ImGui::IsItemActive();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);

        const Bool hasProgress = progress >= 0.0f && progress <= 1.0f;
        const Float playedX = hasProgress ? origin.x + size.x * progress : Float(origin.x);
        const ImU32 played = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        const ImU32 unplayed = ImGui::GetColorU32(ImGuiCol_PlotLines);
        const Float middle = origin.y + size.y * 0.5f;
        const Float half = size.y * 0.5f - 1.0f;
        if (peaks.Size() > 0)
        {
            // 한 픽셀에 한 줄이다. 봉우리가 픽셀보다 많으면 그 사이의 최댓값을 쓴다 - 짧은 딱 소리가 사라지지 않게.
            const Int32 columns = static_cast<JBro::Int32>(size.x);
            for (Int32 column = 0; column < columns; ++column)
            {
                const std::size_t first = static_cast<std::size_t>(column) * peaks.Size() / static_cast<std::size_t>(columns);
                std::size_t last = static_cast<std::size_t>(column + 1) * peaks.Size() / static_cast<std::size_t>(columns);
                if (last <= first)
                {
                    last = first + 1;
                }
                Float peak = 0.0f;
                for (std::size_t index = first; index < last && index < peaks.Size(); ++index)
                {
                    peak = JBro::Max(peak, peaks.Data()[index]);
                }
                const Float x = origin.x + static_cast<JBro::Float>(column) + 0.5f;
                const Float extent = peak * half < 0.5f ? Float(0.5f) : peak * half;
                draw->AddLine(ImVec2(x, middle - extent), ImVec2(x, middle + extent), x <= playedX ? played : unplayed);
            }
        }
        if (hasProgress)
        {
            draw->AddLine(ImVec2(playedX, origin.y), ImVec2(playedX, end.y), ImGui::GetColorU32(ImGuiCol_Text), 1.5f);
        }
        if ((pressed || held) && size.x > 1.0f)
        {
            Float fraction = (ImGui::GetIO().MousePos.x - origin.x) / size.x;
            fraction = fraction < 0.0f ? Float(0.0f) : (fraction > 1.0f ? Float(1.0f) : fraction);
            seekFraction = fraction;
            return true;
        }
        return false;
    }
}
