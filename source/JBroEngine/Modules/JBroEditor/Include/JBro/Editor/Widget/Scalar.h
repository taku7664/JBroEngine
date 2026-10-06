#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <concepts>
#include <type_traits>

namespace JBro::Widget
{
    // 숫자 칸이 받는 값의 타입이다. **엔진 값 타입만** 받는다 - 원시 `float`·`int` 는 컴파일되지 않는다(D-290).
    // 부르는 쪽이 원시 타입을 들고 있으면 그쪽을 엔진 타입으로 바꾼다. 위젯에서 감싸 주지 않는다.
    template<typename T>
    concept FieldNumber = std::same_as<T, Float> || std::same_as<T, Int32> || std::same_as<T, Int64>
        || std::same_as<T, UInt32> || std::same_as<T, UInt64>;

    // 드래그로 굵게 잡고 칸 안 오른쪽 끝의 ▲▼ 로 한 칸씩 맞추는 숫자 칸이다.
    // ▲▼ 는 칸 옆에 따로 붙지 않고 칸 안에 위아래로 쌓인다 - 옆에 -/+ 단추 둘을 두면
    // 칸이 그만큼 좁아지고 줄마다 단추가 늘어서 시끄럽다. 누르고 있으면 반복한다.
    //
    // **값은 생성자로 받는다.** 그래야 타입을 적지 않아도 된다 - `DragField("##count", count)` 가
    // `DragField<Int32>` 다. 타입마다 다른 것(ImGui 자료형·기본 형식·기본 속도)은 안에서 `if constexpr` 로 가른다.
    //
    // **왜 따로 있는가**: 글자 칸은 값을 바꾸려면 타자를 치거나 단추를 여러 번
    // 눌러야 한다. 칸 수·여백·간격처럼 "적당한 값을 찾아 가는" 수치에는 드래그가
    // 훨씬 빠르다. 반대로 드래그만 있으면 정확히 1 을 더하기가 어렵다 - 그래서
    // 둘을 붙였다.
    //
    // 범위가 **열린** 값(개수·픽셀 크기·거리)에만 쓴다. 0~1 로 정규화된 값은
    // `SliderField` 다 - 그쪽은 범위 자체가 작업 영역이라 지금 어디인지가 보여야 한다.
    // `Range` 는 말도 안 되는 값을 막는 울타리이지 작업 영역이 아니다.
    // **단추가 없으면 Id 를 쌓지 않는다.** 칸 하나뿐이면 `id` 가 곧 그 칸의 Id 다 - 인스펙터
    // 테스트가 필드의 Id 를 `##value` 로 셀 수 있어야 하고, 단추가 있을 때만 `id` 아래에
    // `##drag`·`##up`·`##down` 이 쌓인다. 화살표를 그린 뒤에도 마지막 항목은 칸이다.
    //
    // `Format` 은 `Float` 에만 있다. 정수는 폭마다 형식 문자가 달라(`%d`·`%lld`·`%u`) 잘못 주면 값이 깨진다 -
    // 정수 칸은 위젯이 고른 형식을 쓴다.
    template<FieldNumber T>
    class DragField
    {
    public:
        DragField(const char* id, T& value);

        DragField& Range(std::type_identity_t<T> minValue, std::type_identity_t<T> maxValue);
        DragField& Speed(Float unitsPerPixel);
        DragField& Step(std::type_identity_t<T> step);
        DragField& StepButtons(Bool show);
        DragField& Format(const char* format) requires std::same_as<T, Float>;
        DragField& Width(Float width);

        Bool Draw() const;
        Bool operator()() const;

    private:
        const char* m_id = nullptr;
        T& m_value;
        const char* m_format = nullptr;
        T m_min{};
        T m_max{};
        T m_step{};
        Float m_speed = 0.0f;
        Float m_width = 0.0f;
        Bool m_stepButtons = true;
    };

    // 범위가 **닫힌** 값의 슬라이더다. `Range` 어트리뷰트가 붙은 필드가 이것으로 그려진다 -
    // 범위 자체가 작업 영역이라 지금 어디인지가 보여야 한다. 받는 타입과 `Format` 규칙은 `DragField` 와 같다.
    // 범위는 값의 타입으로 맞춰 받는다(`type_identity_t`) - `SliderField("##v", volume, 0.0f, 1.0f)` 에서
    // `0.0f` 가 타입 추론을 흔들지 않고 `Float` 로 들어간다.
    template<FieldNumber T>
    class SliderField
    {
    public:
        SliderField(const char* id, T& value, std::type_identity_t<T> minValue, std::type_identity_t<T> maxValue);

        SliderField& Format(const char* format) requires std::same_as<T, Float>;
        SliderField& Width(Float width);

        Bool Draw() const;
        Bool operator()() const;

    private:
        const char* m_id = nullptr;
        T& m_value;
        const char* m_format = nullptr;
        T m_min{};
        T m_max{};
        Float m_width = 0.0f;
    };

    extern template class DragField<Float>;
    extern template class DragField<Int32>;
    extern template class DragField<Int64>;
    extern template class DragField<UInt32>;
    extern template class DragField<UInt64>;
    extern template class SliderField<Float>;
    extern template class SliderField<Int32>;
    extern template class SliderField<Int64>;
    extern template class SliderField<UInt32>;
    extern template class SliderField<UInt64>;

    // 무게에 물든 버튼이다. 지우기처럼 되돌릴 수 없는 것에 `Error` 를 준다 -
    // 색이 같으면 "저장" 과 "지우기" 가 같은 무게로 보인다.
    class ActionButton
    {
    public:
        explicit ActionButton(const char* label);

        ActionButton& Level(Severity severity);
        ActionButton& Tooltip(const char* text);
        ActionButton& Size(ImVec2 size);
        ActionButton& Disabled(Bool disabled = true);

        Bool Draw() const;
        Bool operator()() const;

    private:
        const char* m_label = nullptr;
        const char* m_tooltip = nullptr;
        Severity m_severity = Severity::Info;
        ImVec2 m_size = ImVec2(0.0f, 0.0f);
        Bool m_disabled = false;
    };

    // 도는 고리. 무언가 오래 걸릴 때 멈춘 것이 아님을 보여 준다.
    //
    // 커서를 지름만큼 전진시키므로 `SameLine` 으로 글자를 이어 붙일 수 있고,
    // `CheckMark` 가 같은 크기를 차지하므로 끝난 뒤 그 자리에 바꿔 놓으면 줄이
    // 흔들리지 않는다.
    void LoadingSpinner(Float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));
    void LoadingSpinnerEx(Float radius, Float thickness, Float spinSpeed, ImVec4 color);
    void CheckMark(Float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));
}
