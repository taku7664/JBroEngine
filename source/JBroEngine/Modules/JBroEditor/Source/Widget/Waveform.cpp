#include <JBro/Editor/Widget/Waveform.h>

#include <JBro/Editor/Widget/Basic.h>

namespace JBro::Widget
{
    bool Waveform(const char* id, ArrayView<const float> peaks, float progress, float height, float& seekFraction)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width > 1.0f ? width : 1.0f, height > 1.0f ? height : 1.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool pressed = HitArea(id, size, ImGuiButtonFlags_MouseButtonLeft);
        const bool held = ImGui::IsItemActive();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);

        const bool hasProgress = progress >= 0.0f && progress <= 1.0f;
        const float playedX = hasProgress ? origin.x + size.x * progress : origin.x;
        const ImU32 played = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        const ImU32 unplayed = ImGui::GetColorU32(ImGuiCol_PlotLines);
        const float middle = origin.y + size.y * 0.5f;
        const float half = size.y * 0.5f - 1.0f;
        if (peaks.Size() > 0)
        {
            // 한 픽셀에 한 줄이다. 봉우리가 픽셀보다 많으면 그 사이의 최댓값을 쓴다 - 짧은 딱 소리가 사라지지 않게.
            const int columns = static_cast<int>(size.x);
            for (int column = 0; column < columns; ++column)
            {
                const std::size_t first = static_cast<std::size_t>(column) * peaks.Size() / static_cast<std::size_t>(columns);
                std::size_t last = static_cast<std::size_t>(column + 1) * peaks.Size() / static_cast<std::size_t>(columns);
                if (last <= first)
                {
                    last = first + 1;
                }
                float peak = 0.0f;
                for (std::size_t index = first; index < last && index < peaks.Size(); ++index)
                {
                    peak = peaks.Data()[index] > peak ? peaks.Data()[index] : peak;
                }
                const float x = origin.x + static_cast<float>(column) + 0.5f;
                const float extent = peak * half < 0.5f ? 0.5f : peak * half;
                draw->AddLine(ImVec2(x, middle - extent), ImVec2(x, middle + extent), x <= playedX ? played : unplayed);
            }
        }
        if (hasProgress)
        {
            draw->AddLine(ImVec2(playedX, origin.y), ImVec2(playedX, end.y), ImGui::GetColorU32(ImGuiCol_Text), 1.5f);
        }
        if ((pressed || held) && size.x > 1.0f)
        {
            float fraction = (ImGui::GetIO().MousePos.x - origin.x) / size.x;
            fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
            seekFraction = fraction;
            return true;
        }
        return false;
    }
}
