#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/Bool.h>

namespace JBro::Widget
{
    // `FormLayout::Row` 의 왼쪽 칸에 서는 라벨이다.
    //
    // 그냥 `TextUnformatted` 와 다른 점: 설명을 달 수 있고, 잠긴 값과 잘못된 값이
    // 서로 다른 색으로 보이고, 반드시 채워야 하는 칸에 별표가 붙는다. 그 셋을
    // 부르는 쪽마다 손으로 그리면 화면마다 다른 색이 나온다.
    class FieldLabel
    {
    public:
        explicit FieldLabel(const char* text);

        FieldLabel& Tooltip(const char* text);
        FieldLabel& Required(Bool required = true);
        FieldLabel& Invalid(Bool invalid = true);
        FieldLabel& Disabled(Bool disabled = true);

        void Draw() const;
        void operator()() const;

    private:
        const char* m_text = nullptr;
        const char* m_tooltip = nullptr;
        Bool m_required = false;
        Bool m_invalid = false;
        Bool m_disabled = false;
    };

    // 구역을 가르는 머리다. 제목이 비면 그냥 선이다.
    class SectionHeader
    {
    public:
        explicit SectionHeader(const char* title);

        SectionHeader& Description(const char* text);
        SectionHeader& SpacingBefore(Bool spacing = true);
        SectionHeader& SpacingAfter(Bool spacing = true);

        void Draw() const;
        void operator()() const;

    private:
        const char* m_title = nullptr;
        const char* m_description = nullptr;
        Bool m_spacingBefore = false;
        Bool m_spacingAfter = true;
    };

    // 무엇이 잘못됐는지 한 줄로 말한다. 무게에 따라 색과 머리글자가 달라진다.
    class ValidationMessage
    {
    public:
        ValidationMessage(Severity severity, const char* text);

        ValidationMessage& Wrapped(Bool wrapped = true);

        void Draw() const;
        void operator()() const;

    private:
        Severity m_severity = Severity::Info;
        const char* m_text = nullptr;
        Bool m_wrapped = true;
    };
}
