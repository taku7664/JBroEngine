#include <JBro/Editor/Widget/Scalar.h>

// IM_PI 와 ImVec2 연산을 쓴다.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace JBro::Widget
{
    namespace
    {
        // **범위가 없으면 붙잡지 않는다.** `ImGuiSliderFlags_AlwaysClamp` 는 `ClampZeroRange` 를
        // 품고 있어 min == max == 0 인 끌기를 0 에 묶는다 - 범위 없는 실수 필드가 끌어도 움직이지
        // 않았다(인스펙터 회전 테스트가 잡았다). 범위가 있을 때만 붙잡는다.
        ImGuiSliderFlags ClampFlags(bool bounded)
        {
            return bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
        }
    }

    namespace
    {
        // 칸 오른쪽 끝의 화살표 한쪽(위 또는 아래)이다. 칸 위에 겹쳐 놓으므로 배치를 밀지 않는다.
        // 누르고 있으면 반복한다(`ButtonRepeat`). 키보드 이동은 칸이 받고 화살표는 건너뛴다.
        bool SpinArrow(const char* id, const ImRect& bb, bool up)
        {
            const ImGuiID itemId = ImGui::GetID(id);
            if (false == ImGui::ItemAdd(bb, itemId, nullptr,
                    ImGuiItemFlags_ButtonRepeat | ImGuiItemFlags_NoNav | ImGuiItemFlags_NoTabStop))
            {
                return false;
            }
            bool hovered = false;
            bool held = false;
            const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (hovered || held)
            {
                const float rounding = ImGui::GetStyle().FrameRounding;
                const ImDrawFlags corners = up ? ImDrawFlags_RoundCornersTopRight : ImDrawFlags_RoundCornersBottomRight;
                draw->AddRectFilled(bb.Min, bb.Max,
                    ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), rounding, corners);
            }
            // 삼각형은 밑변이 칸 폭의 절반, 높이는 밑변의 반이다. 픽셀에 맞춰야 가장자리가 번지지 않는다.
            const float halfWidth = std::floor(bb.GetWidth() * 0.25f);
            const float height = std::max(2.0f, halfWidth);
            const ImVec2 center(std::floor((bb.Min.x + bb.Max.x) * 0.5f), std::floor((bb.Min.y + bb.Max.y) * 0.5f));
            const float top = std::floor(center.y - height * 0.5f) + (up ? 0.0f : 1.0f);
            const float bottom = top + height;
            const ImU32 color = ImGui::GetColorU32(hovered || held ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            if (up)
            {
                draw->AddTriangleFilled(ImVec2(center.x, top), ImVec2(center.x + halfWidth, bottom),
                    ImVec2(center.x - halfWidth, bottom), color);
            }
            else
            {
                draw->AddTriangleFilled(ImVec2(center.x - halfWidth, top), ImVec2(center.x + halfWidth, top),
                    ImVec2(center.x, bottom), color);
            }
            return pressed;
        }

        // 방금 그린 칸 안 오른쪽 끝에 ▲▼ 를 위아래로 쌓는다. 위를 누르면 +1, 아래는 -1, 아니면 0.
        //
        // **화살표를 누른 뒤에도 "마지막 항목" 은 칸이다.** 부르는 쪽이 `IsItemDeactivatedAfterEdit`·
        // 툴팁·가이드의 사각형을 칸 기준으로 본다 - 화살표가 마지막이면 그것이 모두 화살표로 간다.
        int SpinArrows()
        {
            ImGuiContext& context = *ImGui::GetCurrentContext();
            const ImGuiLastItemData field = context.LastItemData;
            const ImRect frame = field.Rect;
            const float width = std::floor(std::max(10.0f, frame.GetHeight() * 0.7f));
            const float middle = std::floor((frame.Min.y + frame.Max.y) * 0.5f);
            const ImRect upper(ImVec2(frame.Max.x - width, frame.Min.y), ImVec2(frame.Max.x, middle));
            const ImRect lower(ImVec2(frame.Max.x - width, middle), frame.Max);

            int direction = 0;
            if (SpinArrow("##up", upper, true))
            {
                direction = 1;
            }
            if (SpinArrow("##down", lower, false))
            {
                direction = -1;
            }
            context.LastItemData = field;
            return direction;
        }
    }

    DragInt::DragInt(const char* id)
        : m_id(id)
    {
    }

    DragInt& DragInt::Range(int minValue, int maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        return *this;
    }

    DragInt& DragInt::Speed(float unitsPerPixel)
    {
        m_speed = unitsPerPixel;
        return *this;
    }

    DragInt& DragInt::Step(int step)
    {
        m_step = step;
        return *this;
    }

    DragInt& DragInt::StepButtons(bool show)
    {
        m_stepButtons = show;
        return *this;
    }

    DragInt& DragInt::Format(const char* format)
    {
        m_format = format;
        return *this;
    }

    DragInt& DragInt::Width(float width)
    {
        m_width = width;
        return *this;
    }

    bool DragInt::Draw(int& value) const
    {
        if (false == m_stepButtons)
        {
            if (m_width > 0.0f)
            {
                ImGui::SetNextItemWidth(m_width);
            }
            return ImGui::DragInt(m_id, &value, m_speed, m_min, m_max, m_format,
                ClampFlags(m_min < m_max));
        }
        ImGui::PushID(m_id);
        // 화살표는 칸 안에 들어가므로 폭을 떼어 내지 않는다. 폭을 주지 않으면 남은 폭을 다 쓴다.
        ImGui::SetNextItemWidth(m_width != 0.0f ? m_width : ImGui::GetContentRegionAvail().x);
        // 화살표가 칸 위에 겹친다. 겹친 쪽이 마우스를 먼저 받도록 칸을 겹칠 수 있게 연다.
        ImGui::SetNextItemAllowOverlap();
        bool changed = ImGui::DragInt("##drag", &value, m_speed, m_min, m_max,
            m_format, ClampFlags(m_min < m_max));

        const int direction = SpinArrows();
        if (direction != 0)
        {
            value += direction * m_step;
            changed = true;
            // **화살표는 드래그의 클램프를 타지 않는다.** 여기서 직접 가둔다.
            if (m_min < m_max)
            {
                value = std::clamp(value, m_min, m_max);
            }
        }
        ImGui::PopID();
        return changed;
    }

    bool DragInt::operator()(int& value) const
    {
        return Draw(value);
    }

    DragFloat::DragFloat(const char* id)
        : m_id(id)
    {
    }

    DragFloat& DragFloat::Range(float minValue, float maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        return *this;
    }

    DragFloat& DragFloat::Speed(float unitsPerPixel)
    {
        m_speed = unitsPerPixel;
        return *this;
    }

    DragFloat& DragFloat::Step(float step)
    {
        m_step = step;
        return *this;
    }

    DragFloat& DragFloat::StepButtons(bool show)
    {
        m_stepButtons = show;
        return *this;
    }

    DragFloat& DragFloat::Format(const char* format)
    {
        m_format = format;
        return *this;
    }

    DragFloat& DragFloat::Width(float width)
    {
        m_width = width;
        return *this;
    }

    bool DragFloat::Draw(float& value) const
    {
        if (false == m_stepButtons)
        {
            // 칸 하나뿐이다. `id` 가 곧 그 칸이다.
            if (m_width > 0.0f)
            {
                ImGui::SetNextItemWidth(m_width);
            }
            return ImGui::DragFloat(m_id, &value, m_speed, m_min, m_max, m_format,
                ClampFlags(m_min < m_max));
        }
        ImGui::PushID(m_id);
        ImGui::SetNextItemWidth(m_width != 0.0f ? m_width : ImGui::GetContentRegionAvail().x);
        ImGui::SetNextItemAllowOverlap();
        bool changed = ImGui::DragFloat("##drag", &value, m_speed, m_min, m_max,
            m_format, ClampFlags(m_min < m_max));

        const int direction = SpinArrows();
        if (direction != 0)
        {
            value += static_cast<float>(direction) * m_step;
            changed = true;
            if (m_min < m_max)
            {
                value = std::clamp(value, m_min, m_max);
            }
        }
        ImGui::PopID();
        return changed;
    }

    bool DragFloat::operator()(float& value) const
    {
        return Draw(value);
    }

    ActionButton::ActionButton(const char* label)
        : m_label(label)
    {
    }

    ActionButton& ActionButton::Level(Severity severity)
    {
        m_severity = severity;
        return *this;
    }

    ActionButton& ActionButton::Tooltip(const char* text)
    {
        m_tooltip = text;
        return *this;
    }

    ActionButton& ActionButton::Size(ImVec2 size)
    {
        m_size = size;
        return *this;
    }

    ActionButton& ActionButton::Disabled(bool disabled)
    {
        m_disabled = disabled;
        return *this;
    }

    bool ActionButton::Draw() const
    {
        const ImVec4 base = SeverityColor(m_severity);
        StyleScope style;
        style.PushColor(ImGuiCol_Button, WithAlpha(base, 0.25f));
        style.PushColor(ImGuiCol_ButtonHovered, WithAlpha(base, 0.40f));
        style.PushColor(ImGuiCol_ButtonActive, WithAlpha(base, 0.55f));

        bool clicked = false;
        {
            DisableScope disable(m_disabled);
            clicked = ImGui::Button(m_label != nullptr ? m_label : "", m_size);
        }
        style.Pop();
        HoveredTooltip(m_tooltip);
        return clicked && false == m_disabled;
    }

    bool ActionButton::operator()() const
    {
        return Draw();
    }

    void LoadingSpinnerEx(float radius, float thickness, float spinSpeed, ImVec4 color)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 padding = style.FramePadding;
        if (radius <= 0.0f)
        {
            radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 center = cursor + padding + ImVec2(radius, radius);

        constexpr int Segments = 20;
        // 꼬리를 잘라 두어야 어느 쪽으로 도는지 보인다. 온전한 고리는 멈춘
        // 것과 구분되지 않는다.
        constexpr int VisibleSegments = Segments - 4;

        const float start = static_cast<float>(ImGui::GetTime()) * spinSpeed;
        const float step = 2.0f * IM_PI / static_cast<float>(Segments);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 packed = ImGui::GetColorU32(color);
        for (int index = 0; index < VisibleSegments; ++index)
        {
            const float angle = start + index * step;
            const ImVec2 from(center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius);
            const ImVec2 to(center.x + std::cos(angle + step) * radius,
                center.y + std::sin(angle + step) * radius);
            drawList->AddLine(from, to, packed, thickness);
        }

        // 커서를 전진시킨다. 부르는 쪽이 `SameLine` 만 하면 다음 위젯이 이어진다.
        ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f) + padding);
    }

    void LoadingSpinner(float radius, ImVec4 color)
    {
        LoadingSpinnerEx(radius, 2.5f, 6.0f, color);
    }

    void CheckMark(float radius, ImVec4 color)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 padding = style.FramePadding;
        if (radius <= 0.0f)
        {
            radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
        }
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 center = cursor + padding + ImVec2(radius, radius);

        // 스피너와 **같은 크기를 차지한다.** 끝난 뒤 그 자리에 바꿔 놓아도
        // 줄이 흔들리지 않게.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 packed = ImGui::GetColorU32(color);
        const ImVec2 left(center.x - radius * 0.55f, center.y);
        const ImVec2 bottom(center.x - radius * 0.15f, center.y + radius * 0.45f);
        const ImVec2 right(center.x + radius * 0.6f, center.y - radius * 0.5f);
        drawList->AddLine(left, bottom, packed, 2.5f);
        drawList->AddLine(bottom, right, packed, 2.5f);

        ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f) + padding);
    }

    bool SliderFloat(const char* id, float& value, float minValue, float maxValue, float width)
    {
        if (width > 0.0f)
        {
            ImGui::SetNextItemWidth(width);
        }
        return ImGui::SliderFloat(id != nullptr ? id : "##slider", &value, minValue, maxValue);
    }

    bool SliderInt(const char* id, int& value, int minValue, int maxValue, float width)
    {
        if (width > 0.0f)
        {
            ImGui::SetNextItemWidth(width);
        }
        return ImGui::SliderInt(id != nullptr ? id : "##slider", &value, minValue, maxValue);
    }
}
