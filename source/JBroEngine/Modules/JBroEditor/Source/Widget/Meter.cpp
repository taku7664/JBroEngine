#include <JBro/Editor/Widget/Meter.h>

#include <JBro/Editor/Widget/Basic.h>

#include <cmath>

namespace JBro::Widget
{
    namespace
    {
        constexpr float FloorDecibels = -60.0f;

        float ToFill(float level)
        {
            if (!(level > 0.0f))
            {
                return 0.0f;
            }
            const float decibels = 20.0f * std::log10(level);
            const float fill = (decibels - FloorDecibels) / -FloorDecibels;
            return fill < 0.0f ? 0.0f : (fill > 1.0f ? 1.0f : fill);
        }
    }

    void LevelMeter(const char* id, float level, float height)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const float barHeight = height > 0.0f ? height : ImGui::GetFrameHeight() * 0.5f;
        const ImVec2 size(width > 1.0f ? width : 1.0f, barHeight);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        const float rounding = ImGui::GetStyle().FrameRounding;
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        const float fill = ToFill(level);
        if (fill > 0.0f)
        {
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x * fill, end.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram),
                rounding);
        }
        if (level > 1.0f)
        {
            // 장치로 가기 전에 잘린다 - 찌그러진 소리가 났다는 뜻이다.
            draw->AddRectFilled(ImVec2(end.x - 4.0f, origin.y), end, ImGui::GetColorU32(ImGuiCol_PlotLinesHovered), rounding);
        }
    }

    void Spectrum(const char* id, ArrayView<const float> bands, float height)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width > 1.0f ? width : 1.0f, height > 1.0f ? height : 1.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        if (bands.Size() == 0)
        {
            return;
        }
        const float column = size.x / static_cast<float>(bands.Size());
        const float gap = column > 3.0f ? 1.0f : 0.0f;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        for (std::size_t band = 0; band < bands.Size(); ++band)
        {
            float value = bands.Data()[band];
            value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            if (value <= 0.0f)
            {
                continue;
            }
            const float left = origin.x + column * static_cast<float>(band);
            draw->AddRectFilled(ImVec2(left, end.y - size.y * value), ImVec2(left + column - gap, end.y), color);
        }
    }
}
