#include <JBro/Editor/Widget/Timeline.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Types/ValueMath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace JBro::Widget
{
    namespace
    {
        // 눈금 줄과 트랙 줄의 높이다. 글자 줄에 위아래 여백을 더한다.
        Float RulerHeight()
        {
            return ImGui::GetTextLineHeight() + 8.0f;
        }

        Float TrackHeight()
        {
            return ImGui::GetTextLineHeight() + 6.0f;
        }

        // 1·2·5 × 10^n 중 `raw` 이상의 가장 작은 값이다(눈금 간격).
        Float NiceSeconds(Float raw)
        {
            const Float safe = raw > 1.0e-4f ? raw : Float(1.0e-4f);
            const Float power = std::pow(10.0f, std::floor(std::log10(safe.Get())));
            for (const Float step : {Float(1.0f), Float(2.0f), Float(5.0f), Float(10.0f)})
            {
                if (step * power >= safe)
                {
                    return step * power;
                }
            }
            return power * 10.0f;
        }

        ImU32 ToColor(const ImVec4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }
    }

    Timeline::Timeline(const char* id, Float duration, Float labelWidth)
    {
        ImGui::PushID(id);
        m_duration = duration > 1.0e-4f ? duration : Float(1.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const Float available = (std::max)(Float(1.0f), Float(ImGui::GetContentRegionAvail().x));
        const Float label = labelWidth > 0.0f ? labelWidth : Float(ImGui::GetFontSize() * 5.5f);
        m_left = origin.x;
        m_top = origin.y;
        m_bottom = origin.y;
        // 시간 칸 양 끝에 표식 반 폭만큼 남긴다 - 0 초와 끝의 표식이 칸 밖으로 잘리지 않는다.
        const Float pad = 6.0f;
        m_trackLeft = origin.x + label + pad;
        m_trackWidth = (std::max)(Float(8.0f), Float(available - label - pad * 2.0f));
    }

    Timeline::~Timeline()
    {
        if (false == m_ended)
        {
            ImGui::PopID();
        }
    }

    Float Timeline::Height(UInt32 tracks)
    {
        const Float gap = ImGui::GetStyle().ItemSpacing.y * 0.5f;
        return RulerHeight() + gap + (TrackHeight() + gap) * static_cast<Float>(tracks.Get());
    }

    Float Timeline::XAt(Float time) const
    {
        return m_trackLeft + JBro::Clamp(time / m_duration, Float(0.0f), Float(1.0f)) * m_trackWidth;
    }

    Float Timeline::TimeAt(Float x) const
    {
        return JBro::Clamp((x - m_trackLeft) / m_trackWidth * m_duration, Float(0.0f), m_duration);
    }

    void Timeline::DrawLabel(const char* icon, const char* label, Float top, Float height) const
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 color = ToColor(EditorTheme::InkMuted);
        const Float line = ImGui::GetTextLineHeight();
        Float x = m_left + ImGui::GetStyle().FramePadding.x;
        if (icon != nullptr && *icon != '\0')
        {
            DrawGlyphCentered(icon, ImVec2(x, top), ImVec2(x + line, top + height), color);
            x += line + 4.0f;
        }
        if (label != nullptr && *label != '\0')
        {
            draw->PushClipRect(ImVec2(m_left, top), ImVec2(m_trackLeft - 4.0f, top + height), true);
            draw->AddText(ImVec2(x, top + (height - line) * 0.5f), color, label);
            draw->PopClipRect();
        }
    }

    void Timeline::NextRow(Float bottom)
    {
        m_bottom = bottom;
        ImGui::SetCursorScreenPos(ImVec2(m_left, bottom + ImGui::GetStyle().ItemSpacing.y * 0.5f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    Bool Timeline::Ruler(Float& time)
    {
        const Float top = m_bottom;
        const Float height = RulerHeight();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min(m_trackLeft - 6.0f, top);
        const ImVec2 max(m_trackLeft + m_trackWidth + 6.0f, top + height);
        draw->AddRectFilled(min, max, ToColor(EditorTheme::Input), ImGui::GetStyle().FrameRounding);
        // 큰 눈금은 글자가 들어갈 만큼(60 픽셀) 띄우고, 그 사이를 다섯으로 나눈 작은 눈금이 6 픽셀 이상이면 긋는다.
        const Float pixelsPerSecond = m_trackWidth / m_duration;
        const Float major = NiceSeconds(60.0f / pixelsPerSecond);
        const Float minor = major / 5.0f;
        const Bool drawMinor = minor * pixelsPerSecond >= 6.0f;
        const ImU32 tickColor = ToColor(EditorTheme::LineStrong);
        const ImU32 textColor = ToColor(EditorTheme::InkMuted);
        const char* format = major >= 1.0f ? "%.0f" : (major >= 0.1f ? "%.1f" : "%.2f");
        const Int32 minorCount = static_cast<Int32>(std::floor((m_duration / minor).Get() + 1.0e-3f));
        for (Int32 index = 0; index <= minorCount; ++index)
        {
            const Float t = minor * static_cast<Float>(index.Get());
            const Float x = XAt(t);
            const Bool isMajor = index.Get() % 5 == 0;
            if (false == isMajor && false == drawMinor)
            {
                continue;
            }
            const Float tick = isMajor ? Float(7.0f) : Float(3.0f);
            draw->AddLine(ImVec2(x, top + height - tick), ImVec2(x, top + height), tickColor);
            if (isMajor)
            {
                char text[24];
                std::snprintf(text, sizeof(text), format, static_cast<double>(t.Get()));
                // 글자는 눈금 오른쪽이다. 칸 끝을 넘으면 눈금 왼쪽에 붙인다 - 끝 눈금의 글자가 잘리지 않는다.
                const Float textWidth = ImGui::CalcTextSize(text).x;
                const Float textX = x + 3.0f + textWidth > max.x ? Float(x - 3.0f - textWidth) : Float(x + 3.0f);
                draw->AddText(ImVec2(textX, top + 1.0f), textColor, text);
            }
        }
        ImGui::SetCursorScreenPos(min);
        HitArea("##timeline_ruler", ImVec2(max.x - min.x, height), ImGuiButtonFlags_MouseButtonLeft);
        Bool changed = false;
        if (ImGui::IsItemActive())
        {
            const Float picked = TimeAt(ImGui::GetIO().MousePos.x);
            changed = picked != time;
            time = picked;
        }
        m_lastRowTop = top;
        m_lastRowHeight = height;
        NextRow(top + height);
        return changed;
    }

    TimelineMarkerEdit Timeline::MarkerTrack(const char* id, const char* icon, const char* label, ArrayView<const Float> times,
        ArrayView<const char* const> names, Int32 selected, const char* emptyTooltip)
    {
        TimelineMarkerEdit edit;
        const Widget::IdScope scope(id);
        const Float top = m_bottom;
        const Float height = TrackHeight();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawLabel(icon, label, top, height);
        const ImVec2 min(m_trackLeft - 6.0f, top);
        const ImVec2 max(m_trackLeft + m_trackWidth + 6.0f, top + height);
        draw->AddRectFilled(min, max, ToColor(EditorTheme::Fade(EditorTheme::Input, 0.55f)), ImGui::GetStyle().FrameRounding);
        // 빈 자리를 두 번 누르면 그 시간에 더한다. 표식이 위에 서도록 겹침을 허락한다.
        ImGui::SetCursorScreenPos(min);
        ImGui::SetNextItemAllowOverlap();
        HitArea("##track", ImVec2(max.x - min.x, height), ImGuiButtonFlags_MouseButtonLeft);
        const Bool trackHovered = ImGui::IsItemHovered();
        if (trackHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            edit.add = true;
            edit.addTime = TimeAt(ImGui::GetIO().MousePos.x);
        }
        Bool markerHovered = false;
        const Float half = 5.0f;
        for (std::size_t index = 0; index < times.Size(); ++index)
        {
            const Float x = XAt(times[index]);
            const Widget::IdScope markerScope(static_cast<Int32>(index));
            ImGui::SetCursorScreenPos(ImVec2(x - half, top));
            HitArea("##marker", ImVec2(half * 2.0f, height), ImGuiButtonFlags_MouseButtonLeft);
            const Bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated())
            {
                edit.pressed = static_cast<Int32>(index);
            }
            if (ImGui::IsItemActive())
            {
                edit.dragging = static_cast<Int32>(index);
                edit.dragTime = TimeAt(ImGui::GetIO().MousePos.x);
            }
            if (ImGui::IsItemDeactivated())
            {
                edit.released = static_cast<Int32>(index);
            }
            if (hovered)
            {
                markerHovered = true;
                const char* name = index < names.Size() ? names[index] : nullptr;
                if (name != nullptr && *name != '\0')
                {
                    ShowTooltip(name);
                }
            }
            // 줄기와 마름모다. 고른 것은 상호작용 색, 올린 것은 밝은 글자색, 나머지는 흐린 글자색이다.
            const Bool isSelected = selected == static_cast<Int32>(index) || edit.dragging == static_cast<Int32>(index);
            const ImU32 color = ToColor(isSelected ? EditorTheme::Accent : (hovered ? EditorTheme::Ink : EditorTheme::InkMuted));
            const Float centerY = top + height * 0.5f;
            draw->AddLine(ImVec2(x, top + 2.0f), ImVec2(x, top + height - 2.0f), color);
            draw->AddQuadFilled(ImVec2(x, centerY - 4.5f), ImVec2(x + 4.5f, centerY), ImVec2(x, centerY + 4.5f), ImVec2(x - 4.5f, centerY), color);
        }
        if (trackHovered && false == markerHovered)
        {
            HoveredTooltip(emptyTooltip);
        }
        m_lastRowTop = top;
        m_lastRowHeight = height;
        NextRow(top + height);
        return edit;
    }

    void Timeline::End(Float time)
    {
        if (m_ended)
        {
            return;
        }
        // 재생 선은 눈금 줄 위부터 마지막 줄 아래까지다. 눈금 줄 위에 머리(아래로 향한 삼각형)를 둔다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const Float x = XAt(time);
        const ImU32 color = ToColor(EditorTheme::Accent);
        draw->AddLine(ImVec2(x, m_top), ImVec2(x, m_bottom), color, 1.5f);
        draw->AddTriangleFilled(ImVec2(x - 5.0f, m_top), ImVec2(x + 5.0f, m_top), ImVec2(x, m_top + 6.0f), color);
        m_ended = true;
        ImGui::PopID();
    }
}
