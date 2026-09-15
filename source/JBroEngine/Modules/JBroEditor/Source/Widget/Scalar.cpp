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
        // -/+ 를 붙일 자리를 떼어 내고 드래그 칸에 남는 폭을 돌려준다.
        float SplitWidthForStepButtons(
            float requested, bool stepButtons, float& outButtonSize, float& outSpacing)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float full =
                requested != 0.0f ? requested : ImGui::GetContentRegionAvail().x;
            if (false == stepButtons)
            {
                outButtonSize = 0.0f;
                outSpacing = 0.0f;
                return std::max(1.0f, full);
            }
            outButtonSize = ImGui::GetFrameHeight();
            outSpacing = style.ItemInnerSpacing.x;
            return std::max(1.0f, full - (outButtonSize + outSpacing) * 2.0f);
        }

        bool StepButton(const char* label, float size)
        {
            return ImGui::Button(label, ImVec2(size, size));
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
        float buttonSize = 0.0f;
        float spacing = 0.0f;
        const float dragWidth =
            SplitWidthForStepButtons(m_width, m_stepButtons, buttonSize, spacing);

        ImGui::SetNextItemWidth(dragWidth);
        bool changed = ImGui::DragInt("##drag", &value, m_speed, m_min, m_max,
            m_format, ClampFlags(m_min < m_max));

        if (m_stepButtons)
        {
            ImGui::SameLine(0.0f, spacing);
            if (StepButton("-", buttonSize))
            {
                value -= m_step;
                changed = true;
            }
            ImGui::SameLine(0.0f, spacing);
            if (StepButton("+", buttonSize))
            {
                value += m_step;
                changed = true;
            }
            // **버튼은 드래그의 클램프를 타지 않는다.** 여기서 직접 가둔다.
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
        float buttonSize = 0.0f;
        float spacing = 0.0f;
        const float dragWidth =
            SplitWidthForStepButtons(m_width, m_stepButtons, buttonSize, spacing);

        ImGui::SetNextItemWidth(dragWidth);
        bool changed = ImGui::DragFloat("##drag", &value, m_speed, m_min, m_max,
            m_format, ClampFlags(m_min < m_max));

        if (m_stepButtons)
        {
            ImGui::SameLine(0.0f, spacing);
            if (StepButton("-", buttonSize))
            {
                value -= m_step;
                changed = true;
            }
            ImGui::SameLine(0.0f, spacing);
            if (StepButton("+", buttonSize))
            {
                value += m_step;
                changed = true;
            }
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
