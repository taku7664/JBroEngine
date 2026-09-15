#pragma once

#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    // 드래그로 굵게 잡고 -/+ 로 한 칸씩 맞추는 숫자 칸이다.
    //
    // **왜 따로 있는가**: `InputInt` 는 값을 바꾸려면 타자를 치거나 -/+ 를 여러 번
    // 눌러야 한다. 칸 수·여백·간격처럼 "적당한 값을 찾아 가는" 수치에는 드래그가
    // 훨씬 빠르다. 반대로 드래그만 있으면 정확히 1 을 더하기가 어렵다 - 그래서
    // 둘을 붙였다.
    //
    // 범위가 **열린** 값(개수·픽셀 크기·거리)에만 쓴다. 0~1 로 정규화된 값은
    // 슬라이더다 - 그쪽은 범위 자체가 작업 영역이라 지금 어디인지가 보여야 한다.
    // `Range` 는 말도 안 되는 값을 막는 울타리이지 작업 영역이 아니다.
    // **단추가 없으면 Id 를 쌓지 않는다.** 칸 하나뿐이면 `id` 가 곧 그 칸의 Id 다 - 인스펙터
    // 테스트가 필드의 Id 를 `##value` 로 셀 수 있어야 하고, 단추가 있을 때만 `id` 아래에
    // `##drag`·`-`·`+` 가 쌓인다.
    class DragInt
    {
    public:
        explicit DragInt(const char* id);

        DragInt& Range(int minValue, int maxValue);
        DragInt& Speed(float unitsPerPixel);
        DragInt& Step(int step);
        DragInt& StepButtons(bool show);
        DragInt& Format(const char* format);
        DragInt& Width(float width);

        bool Draw(int& value) const;
        bool operator()(int& value) const;

    private:
        const char* m_id = nullptr;
        const char* m_format = "%d";
        int m_min = 0;
        int m_max = 0;
        int m_step = 1;
        float m_speed = 0.25f;
        float m_width = 0.0f;
        bool m_stepButtons = true;
    };

    class DragFloat
    {
    public:
        explicit DragFloat(const char* id);

        DragFloat& Range(float minValue, float maxValue);
        DragFloat& Speed(float unitsPerPixel);
        DragFloat& Step(float step);
        DragFloat& StepButtons(bool show);
        DragFloat& Format(const char* format);
        DragFloat& Width(float width);

        bool Draw(float& value) const;
        bool operator()(float& value) const;

    private:
        const char* m_id = nullptr;
        const char* m_format = "%.2f";
        float m_min = 0.0f;
        float m_max = 0.0f;
        float m_step = 1.0f;
        float m_speed = 0.5f;
        float m_width = 0.0f;
        bool m_stepButtons = true;
    };

    // 범위가 **닫힌** 값의 슬라이더다. `Range` 어트리뷰트가 붙은 필드가 이것으로 그려진다 -
    // 범위 자체가 작업 영역이라 지금 어디인지가 보여야 한다.
    bool SliderFloat(const char* id, float& value, float minValue, float maxValue,
        float width = 0.0f);
    bool SliderInt(const char* id, int& value, int minValue, int maxValue, float width = 0.0f);

    // 무게에 물든 버튼이다. 지우기처럼 되돌릴 수 없는 것에 `Error` 를 준다 -
    // 색이 같으면 "저장" 과 "지우기" 가 같은 무게로 보인다.
    class ActionButton
    {
    public:
        explicit ActionButton(const char* label);

        ActionButton& Level(Severity severity);
        ActionButton& Tooltip(const char* text);
        ActionButton& Size(ImVec2 size);
        ActionButton& Disabled(bool disabled = true);

        bool Draw() const;
        bool operator()() const;

    private:
        const char* m_label = nullptr;
        const char* m_tooltip = nullptr;
        Severity m_severity = Severity::Info;
        ImVec2 m_size = ImVec2(0.0f, 0.0f);
        bool m_disabled = false;
    };

    // 도는 고리. 무언가 오래 걸릴 때 멈춘 것이 아님을 보여 준다.
    //
    // 커서를 지름만큼 전진시키므로 `SameLine` 으로 글자를 이어 붙일 수 있고,
    // `CheckMark` 가 같은 크기를 차지하므로 끝난 뒤 그 자리에 바꿔 놓으면 줄이
    // 흔들리지 않는다.
    void LoadingSpinner(float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));
    void LoadingSpinnerEx(float radius, float thickness, float spinSpeed, ImVec4 color);
    void CheckMark(float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));
}
