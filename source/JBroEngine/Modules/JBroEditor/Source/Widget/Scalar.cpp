#include <JBro/Editor/Widget/Scalar.h>

// IM_PI 와 ImVec2 연산을 쓴다.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    namespace
    {
        // **범위가 없으면 붙잡지 않는다.** `ImGuiSliderFlags_AlwaysClamp` 는 `ClampZeroRange` 를
        // 품고 있어 min == max == 0 인 끌기를 0 에 묶는다 - 범위 없는 실수 필드가 끌어도 움직이지
        // 않았다(인스펙터 회전 테스트가 잡았다). 범위가 있을 때만 붙잡는다.
        ImGuiSliderFlags ClampFlags(Bool bounded)
        {
            return bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
        }
    }

    namespace
    {
        // 칸 오른쪽 끝의 화살표 한쪽(위 또는 아래)이다. 칸 위에 겹쳐 놓으므로 배치를 밀지 않는다.
        // 누르고 있으면 반복한다(`ButtonRepeat`). 키보드 이동은 칸이 받고 화살표는 건너뛴다.
        Bool SpinArrow(const char* id, const ImRect& bb, Bool up)
        {
            const ImGuiID itemId = ImGui::GetID(id);
            if (false == ImGui::ItemAdd(bb, itemId, nullptr,
                    ImGuiItemFlags_ButtonRepeat | ImGuiItemFlags_NoNav | ImGuiItemFlags_NoTabStop))
            {
                return false;
            }
            bool hovered = false;
            bool held = false;
            const Bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (hovered || held)
            {
                const Float rounding = ImGui::GetStyle().FrameRounding;
                const ImDrawFlags corners = up ? ImDrawFlags_RoundCornersTopRight : ImDrawFlags_RoundCornersBottomRight;
                draw->AddRectFilled(bb.Min, bb.Max,
                    ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), rounding, corners);
            }
            // 삼각형은 밑변이 칸 폭의 절반, 높이는 밑변의 반이다. 픽셀에 맞춰야 가장자리가 번지지 않는다.
            const Float halfWidth = std::floor(bb.GetWidth() * 0.25f);
            const Float height = JBro::Max(2.0f, halfWidth);
            const ImVec2 center(std::floor((bb.Min.x + bb.Max.x) * 0.5f), std::floor((bb.Min.y + bb.Max.y) * 0.5f));
            const Float top = std::floor(center.y - height * 0.5f) + (up ? 0.0f : 1.0f);
            const Float bottom = top + height;
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
        Int32 SpinArrows()
        {
            ImGuiContext& context = *ImGui::GetCurrentContext();
            const ImGuiLastItemData field = context.LastItemData;
            const ImRect frame = field.Rect;
            const Float width = std::floor(std::max(10.0f, frame.GetHeight() * 0.7f));
            const Float middle = std::floor((frame.Min.y + frame.Max.y) * 0.5f);
            const ImRect upper(ImVec2(frame.Max.x - width, frame.Min.y), ImVec2(frame.Max.x, middle));
            const ImRect lower(ImVec2(frame.Max.x - width, middle), frame.Max);

            Int32 direction = 0;
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

    namespace
    {
        // ImGui 가 읽는 원시 값의 타입이다. 정수 강타입은 `ValueType` 을, `Float` 는 `float` 를 든다.
        template<FieldNumber T>
        struct NumberTraits
        {
            using Raw = typename T::ValueType;
        };

        template<>
        struct NumberTraits<Float>
        {
            using Raw = Float;
        };

        template<FieldNumber T>
        constexpr ImGuiDataType DataTypeOf()
        {
            if constexpr (std::same_as<T, Float>)
            {
                return ImGuiDataType_Float;
            }
            else if constexpr (std::same_as<T, Int32>)
            {
                return ImGuiDataType_S32;
            }
            else if constexpr (std::same_as<T, Int64>)
            {
                return ImGuiDataType_S64;
            }
            else if constexpr (std::same_as<T, UInt32>)
            {
                return ImGuiDataType_U32;
            }
            else
            {
                return ImGuiDataType_U64;
            }
        }

        // **정수 형식은 폭이 고른다.** `%d` 로 64 비트를 찍으면 값이 깨진다.
        template<FieldNumber T>
        constexpr const char* DefaultFormatOf()
        {
            if constexpr (std::same_as<T, Float>)
            {
                return "%.2f";
            }
            else if constexpr (std::same_as<T, Int32>)
            {
                return "%d";
            }
            else if constexpr (std::same_as<T, Int64>)
            {
                return "%lld";
            }
            else if constexpr (std::same_as<T, UInt32>)
            {
                return "%u";
            }
            else
            {
                return "%llu";
            }
        }

        // 정수는 한 칸이 1 이라 픽셀당 0.25 칸, 실수는 0.5 다(옛 `DragInt`·`DragFloat` 의 기본값).
        template<FieldNumber T>
        constexpr Float DefaultSpeedOf()
        {
            if constexpr (std::same_as<T, Float>)
            {
                return 0.5f;
            }
            else
            {
                return 0.25f;
            }
        }

        // ▲▼ 한 칸을 더하고 범위로 가둔다. 부호 없는 값이 0 아래로 내려가 최댓값으로 감기지 않게
        // 빼기는 바닥을 먼저 본다.
        template<FieldNumber T>
        void StepValue(T& value, T step, Int32 direction, T minValue, T maxValue)
        {
            using Raw = typename NumberTraits<T>::Raw;
            const Raw current = value.Get();
            const Raw amount = step.Get();
            const Bool bounded = minValue.Get() < maxValue.Get();
            Raw next = current;
            if (direction > 0)
            {
                next = current + amount;
                if (bounded && next > maxValue.Get())
                {
                    next = maxValue.Get();
                }
            }
            else if (direction < 0)
            {
                if constexpr (std::is_unsigned_v<Raw>)
                {
                    next = current < amount ? Raw(0) : Raw(current - amount);
                }
                else
                {
                    next = current - amount;
                }
                if (bounded && next < minValue.Get())
                {
                    next = minValue.Get();
                }
            }
            value = next;
        }
    }

    template<FieldNumber T>
    DragField<T>::DragField(const char* id, T& value)
        : m_id(id)
        , m_value(value)
        , m_format(DefaultFormatOf<T>())
        , m_step(typename NumberTraits<T>::Raw(1))
        , m_speed(DefaultSpeedOf<T>())
    {
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::Range(std::type_identity_t<T> minValue, std::type_identity_t<T> maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        return *this;
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::Speed(Float unitsPerPixel)
    {
        m_speed = unitsPerPixel;
        return *this;
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::Step(std::type_identity_t<T> step)
    {
        m_step = step;
        return *this;
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::StepButtons(Bool show)
    {
        m_stepButtons = show;
        return *this;
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::Format(const char* format) requires std::same_as<T, Float>
    {
        m_format = format;
        return *this;
    }

    template<FieldNumber T>
    DragField<T>& DragField<T>::Width(Float width)
    {
        m_width = width;
        return *this;
    }

    template<FieldNumber T>
    Bool DragField<T>::Draw() const
    {
        using Raw = typename NumberTraits<T>::Raw;
        // ImGui 는 원시 값의 주소를 받는다. 엔진 타입에서 떠서 넘기고 바뀐 것만 되돌려 쓴다.
        Raw raw = m_value.Get();
        const Raw minRaw = m_min.Get();
        const Raw maxRaw = m_max.Get();
        const Bool bounded = minRaw < maxRaw;
        const ImGuiSliderFlags flags = ClampFlags(bounded);

        if (false == m_stepButtons)
        {
            // 칸 하나뿐이다. `id` 가 곧 그 칸이다.
            if (m_width > 0.0f)
            {
                ImGui::SetNextItemWidth(m_width);
            }
            const Bool changed = ImGui::DragScalar(m_id, DataTypeOf<T>(), &raw, m_speed,
                bounded ? &minRaw : nullptr, bounded ? &maxRaw : nullptr, m_format, flags);
            if (changed)
            {
                m_value = raw;
            }
            return changed;
        }
        ImGui::PushID(m_id);
        // 화살표는 칸 안에 들어가므로 폭을 떼어 내지 않는다. 폭을 주지 않으면 남은 폭을 다 쓴다.
        ImGui::SetNextItemWidth(m_width != 0.0f ? m_width.Get() : ImGui::GetContentRegionAvail().x);
        // 화살표가 칸 위에 겹친다. 겹친 쪽이 마우스를 먼저 받도록 칸을 겹칠 수 있게 연다.
        ImGui::SetNextItemAllowOverlap();
        Bool changed = ImGui::DragScalar("##drag", DataTypeOf<T>(), &raw, m_speed,
            bounded ? &minRaw : nullptr, bounded ? &maxRaw : nullptr, m_format, flags);
        if (changed)
        {
            m_value = raw;
        }

        const Int32 direction = SpinArrows();
        if (direction != 0)
        {
            // **화살표는 드래그의 클램프를 타지 않는다.** 여기서 직접 가둔다.
            StepValue(m_value, m_step, direction, m_min, m_max);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    template<FieldNumber T>
    Bool DragField<T>::operator()() const
    {
        return Draw();
    }

    template<FieldNumber T>
    SliderField<T>::SliderField(const char* id, T& value, std::type_identity_t<T> minValue, std::type_identity_t<T> maxValue)
        : m_id(id)
        , m_value(value)
        , m_format(DefaultFormatOf<T>())
        , m_min(minValue)
        , m_max(maxValue)
    {
        if constexpr (std::same_as<T, Float>)
        {
            // 슬라이더는 범위가 좁아 소수 셋째 자리까지 보인다(ImGui `SliderFloat` 의 기본).
            m_format = "%.3f";
        }
    }

    template<FieldNumber T>
    SliderField<T>& SliderField<T>::Format(const char* format) requires std::same_as<T, Float>
    {
        m_format = format;
        return *this;
    }

    template<FieldNumber T>
    SliderField<T>& SliderField<T>::Width(Float width)
    {
        m_width = width;
        return *this;
    }

    template<FieldNumber T>
    Bool SliderField<T>::Draw() const
    {
        using Raw = typename NumberTraits<T>::Raw;
        if (m_width > 0.0f)
        {
            ImGui::SetNextItemWidth(m_width);
        }
        Raw raw = m_value.Get();
        const Raw minRaw = m_min.Get();
        const Raw maxRaw = m_max.Get();
        const Bool changed = ImGui::SliderScalar(m_id != nullptr ? m_id : "##slider", DataTypeOf<T>(), &raw,
            &minRaw, &maxRaw, m_format);
        if (changed)
        {
            m_value = raw;
        }
        return changed;
    }

    template<FieldNumber T>
    Bool SliderField<T>::operator()() const
    {
        return Draw();
    }

    template class DragField<Float>;
    template class DragField<Int32>;
    template class DragField<Int64>;
    template class DragField<UInt32>;
    template class DragField<UInt64>;
    template class SliderField<Float>;
    template class SliderField<Int32>;
    template class SliderField<Int64>;
    template class SliderField<UInt32>;
    template class SliderField<UInt64>;

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

    ActionButton& ActionButton::Disabled(Bool disabled)
    {
        m_disabled = disabled;
        return *this;
    }

    Bool ActionButton::Draw() const
    {
        const ImVec4 base = SeverityColor(m_severity);
        StyleScope style;
        style.PushColor(ImGuiCol_Button, WithAlpha(base, 0.25f));
        style.PushColor(ImGuiCol_ButtonHovered, WithAlpha(base, 0.40f));
        style.PushColor(ImGuiCol_ButtonActive, WithAlpha(base, 0.55f));

        Bool clicked = false;
        {
            DisableScope disable(m_disabled);
            clicked = ImGui::Button(m_label != nullptr ? m_label : "", m_size);
        }
        style.Pop();
        HoveredTooltip(m_tooltip);
        return clicked && false == m_disabled;
    }

    Bool ActionButton::operator()() const
    {
        return Draw();
    }

    void LoadingSpinnerEx(Float radius, Float thickness, Float spinSpeed, ImVec4 color)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 padding = style.FramePadding;
        if (radius <= 0.0f)
        {
            radius = ImGui::GetFrameHeight() * 0.5f - padding.y;
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 center = cursor + padding + ImVec2(radius, radius);

        constexpr Int32 Segments = 20;
        // 꼬리를 잘라 두어야 어느 쪽으로 도는지 보인다. 온전한 고리는 멈춘
        // 것과 구분되지 않는다.
        constexpr Int32 VisibleSegments = Segments - 4;

        const Float start = static_cast<float>(ImGui::GetTime()) * spinSpeed;
        const Float step = 2.0f * IM_PI / static_cast<float>(Segments);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 packed = ImGui::GetColorU32(color);
        for (Int32 index = 0; index < VisibleSegments; ++index)
        {
            const Float angle = start + index * step;
            const ImVec2 from(center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius);
            const ImVec2 to(center.x + std::cos(angle + step) * radius,
                center.y + std::sin(angle + step) * radius);
            drawList->AddLine(from, to, packed, thickness);
        }

        // 커서를 전진시킨다. 부르는 쪽이 `SameLine` 만 하면 다음 위젯이 이어진다.
        ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f) + padding);
    }

    void LoadingSpinner(Float radius, ImVec4 color)
    {
        LoadingSpinnerEx(radius, 2.5f, 6.0f, color);
    }

    void CheckMark(Float radius, ImVec4 color)
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
}
