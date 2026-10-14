#include <JBro/Editor/Widget/Graph.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>

#include <cfloat>
#include <cmath>

namespace JBro::Widget
{
    namespace
    {
        // 축 값을 0..1 로 옮긴다. 범위 밖은 끝에 붙인다.
        Float ToUnit(const GraphAxis& axis, Float value)
        {
            Float unit = 0.0f;
            if (axis.logarithmic && axis.min > 0.0f && axis.max > axis.min)
            {
                const Float clamped = value > axis.min ? value : axis.min;
                unit = std::log(clamped / axis.min) / std::log(axis.max / axis.min);
            }
            else if (axis.max != axis.min)
            {
                unit = (value - axis.min) / (axis.max - axis.min);
            }
            if (!(unit >= 0.0f))
            {
                return 0.0f;
            }
            return unit > 1.0f ? Float(1.0f) : unit;
        }
    }

    void Graph(const char* id, const GraphDesc& desc)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const Float width = ImGui::GetContentRegionAvail().x;
        const Float height = desc.height > 0.0f ? desc.height : Float(ImGui::GetFrameHeight() * 6.0f);
        const ImVec2 size(width > 1.0f ? width : Float(1.0f), height);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        HitArea(id, size, ImGuiButtonFlags_None);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(EditorTheme::Workspace), style.FrameRounding);
        draw->AddRect(origin, end, ImGui::GetColorU32(EditorTheme::Line), style.FrameRounding);

        // 눈금 글자의 자리를 먼저 뗀다 - 세로 축 글자는 왼쪽, 가로 축 글자는 아래다.
        const Float small = ImGui::GetFontSize() * 0.85f;
        ImFont* font = ImGui::GetFont();
        Float labelWidth = 0.0f;
        for (std::size_t index = 0; index < desc.y.ticks.Size(); ++index)
        {
            const char* label = desc.y.ticks.Data()[index].label;
            if (label != nullptr)
            {
                const Float measured = font->CalcTextSizeA(small, FLT_MAX, 0.0f, label).x;
                labelWidth = measured > labelWidth ? measured : labelWidth;
            }
        }
        const Float pad = 6.0f;
        // 제목은 그림 칸 위의 제 줄에 둔다 - 그림 안에 두면 0 의 막대나 곡선이 글자를 긋는다.
        const Float titleHeight = desc.title != nullptr ? Float(small + 4.0f) : Float(0.0f);
        const ImVec2 plotMin(origin.x + pad + (labelWidth > 0.0f ? Float(labelWidth + 4.0f) : Float(0.0f)), origin.y + pad + titleHeight);
        const ImVec2 plotMax(end.x - pad, end.y - pad - (desc.x.ticks.Size() > 0 ? Float(small + 2.0f) : Float(0.0f)));
        if (plotMax.x <= plotMin.x + 4.0f || plotMax.y <= plotMin.y + 4.0f)
        {
            return;
        }
        const auto toScreen = [&](Float x, Float y) {
            return ImVec2(plotMin.x + (plotMax.x - plotMin.x) * ToUnit(desc.x, x),
                plotMax.y - (plotMax.y - plotMin.y) * ToUnit(desc.y, y));
        };

        const ImU32 gridColor = ImGui::GetColorU32(EditorTheme::Line);
        const ImU32 labelColor = ImGui::GetColorU32(EditorTheme::InkMuted);
        for (std::size_t index = 0; index < desc.x.ticks.Size(); ++index)
        {
            const GraphTick& tick = desc.x.ticks.Data()[index];
            const Float x = toScreen(tick.value, desc.y.min).x;
            draw->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), gridColor);
            if (tick.label != nullptr)
            {
                const Float textWidth = font->CalcTextSizeA(small, FLT_MAX, 0.0f, tick.label).x;
                Float left = x - textWidth * 0.5f;
                if (left < plotMin.x)
                {
                    left = plotMin.x;
                }
                if (left + textWidth > plotMax.x)
                {
                    left = plotMax.x - textWidth;
                }
                draw->AddText(font, small, ImVec2(left, plotMax.y + 2.0f), labelColor, tick.label);
            }
        }
        for (std::size_t index = 0; index < desc.y.ticks.Size(); ++index)
        {
            const GraphTick& tick = desc.y.ticks.Data()[index];
            const Float y = toScreen(desc.x.min, tick.value).y;
            draw->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), gridColor);
            if (tick.label != nullptr)
            {
                const Float textWidth = font->CalcTextSizeA(small, FLT_MAX, 0.0f, tick.label).x;
                draw->AddText(font, small, ImVec2(plotMin.x - 4.0f - textWidth, y - small * 0.5f), labelColor, tick.label);
            }
        }

        draw->PushClipRect(plotMin, plotMax, true);
        const ImU32 markerColor = ImGui::GetColorU32(EditorTheme::Fade(EditorTheme::Amber, 0.8f));
        for (std::size_t index = 0; index < desc.markers.Size(); ++index)
        {
            const Float x = toScreen(desc.markers.Data()[index], desc.y.min).x;
            for (Float y = plotMin.y; y < plotMax.y; y += 6.0f)
            {
                const Float dashEnd = y + 3.0f < plotMax.y ? Float(y + 3.0f) : Float(plotMax.y);
                draw->AddLine(ImVec2(x, y), ImVec2(x, dashEnd), markerColor);
            }
        }
        const ImU32 accent = ImGui::GetColorU32(EditorTheme::AccentBright);
        const ImU32 fill = ImGui::GetColorU32(EditorTheme::Fade(EditorTheme::Accent, 0.22f));
        const ImU32 guide = ImGui::GetColorU32(EditorTheme::Fade(EditorTheme::InkMuted, 0.7f));
        for (std::size_t seriesIndex = 0; seriesIndex < desc.series.Size(); ++seriesIndex)
        {
            const GraphSeries& series = desc.series.Data()[seriesIndex];
            const std::size_t count = series.xs.Size() < series.ys.Size() ? series.xs.Size() : series.ys.Size();
            if (count == 0)
            {
                continue;
            }
            if (series.style == GraphStyle::Stems)
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    const ImVec2 top = toScreen(series.xs.Data()[index], series.ys.Data()[index]);
                    const ImVec2 bottom = toScreen(series.xs.Data()[index], series.baseline);
                    draw->AddLine(bottom, top, accent, 2.0f);
                    draw->AddCircleFilled(top, 3.0f, accent);
                }
                continue;
            }
            if (series.style == GraphStyle::Area)
            {
                // 칸마다 사각형 하나다. 다각형 하나로 칠하면 오목한 곳에서 ImGui 의 볼록 채우기가 엉뚱한 곳을 칠한다.
                for (std::size_t index = 1; index < count; ++index)
                {
                    const ImVec2 a = toScreen(series.xs.Data()[index - 1], series.ys.Data()[index - 1]);
                    const ImVec2 b = toScreen(series.xs.Data()[index], series.ys.Data()[index]);
                    const ImVec2 baseA = toScreen(series.xs.Data()[index - 1], series.baseline);
                    const ImVec2 baseB = toScreen(series.xs.Data()[index], series.baseline);
                    draw->AddQuadFilled(a, b, baseB, baseA, fill);
                }
            }
            const Bool isGuide = series.style == GraphStyle::Guide;
            for (std::size_t index = 1; index < count; ++index)
            {
                draw->AddLine(toScreen(series.xs.Data()[index - 1], series.ys.Data()[index - 1]),
                    toScreen(series.xs.Data()[index], series.ys.Data()[index]), isGuide ? guide : accent,
                    isGuide ? 1.0f : 2.0f);
            }
        }
        draw->PopClipRect();
        if (desc.title != nullptr)
        {
            draw->AddText(font, small, ImVec2(origin.x + pad, origin.y + pad - 2.0f), labelColor, desc.title);
        }
    }
}
