#include <JBro/Editor/Widget/Meter.h>

#include <JBro/Editor/Widget/Basic.h>

#include <cmath>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    namespace
    {
        constexpr Float FloorDecibels = -60.0f;

        Float ToFill(Float level)
        {
            if (!(level > 0.0f))
            {
                return 0.0f;
            }
            const Float decibels = 20.0f * std::log10(level);
            const Float fill = (decibels - FloorDecibels) / -FloorDecibels;
            return fill < 0.0f ? Float(0.0f) : (fill > 1.0f ? Float(1.0f) : fill);
        }
    }

    void LevelMeter(const char* id, Float level, Float height)
    {
        const Float width = ImGui::GetContentRegionAvail().x;
        const Float barHeight = height > 0.0f ? height : Float(ImGui::GetFrameHeight() * 0.5f);
        const ImVec2 size(width > 1.0f ? width : Float(1.0f), barHeight);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        const Float rounding = ImGui::GetStyle().FrameRounding;
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        const Float fill = ToFill(level);
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

    void Spectrum(const char* id, ArrayView<const float> bands, Float height)
    {
        const Float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width > 1.0f ? width : Float(1.0f), height > 1.0f ? height : Float(1.0f));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        if (bands.Size() == 0)
        {
            return;
        }
        const Float column = size.x / static_cast<float>(bands.Size());
        const Float gap = column > 3.0f ? 1.0f : 0.0f;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        for (std::size_t band = 0; band < bands.Size(); ++band)
        {
            Float value = bands.Data()[band];
            value = value < 0.0f ? Float(0.0f) : (value > 1.0f ? Float(1.0f) : value);
            if (value <= 0.0f)
            {
                continue;
            }
            const Float left = origin.x + column * static_cast<float>(band);
            draw->AddRectFilled(ImVec2(left, end.y - size.y * value), ImVec2(left + column - gap, end.y), color);
        }
    }
}
