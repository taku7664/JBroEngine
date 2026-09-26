#pragma once

#include <JBro/Reflection/EnumDescriptor.h>

#include <cstdint>

// 텍스트 컴포넌트가 함께 쓰는 배치 값이다(D-222). 2D `Text2D` 와 3D `Text3D` 가 같은 뜻으로 쓰므로 차원과 무관한 여기에 한 번만 둔다.
// 반사 이름(`Component::TextOverflow` 따위)은 옮기기 전과 같다 - 저장한 캔버스가 그대로 읽힌다.

namespace JBro::Component
{
    // 상자를 넘는 줄을 어떻게 하나(text-plan §4.2). Wrap 과 Clip 은 상자 폭에서 줄을 바꾸고, Clip 은 상자 높이 밖의 줄을 버린다.
    enum class TextOverflow : std::uint8_t { Overflow, Wrap, Clip };
    // 어디서 줄을 바꿀 수 있나. Word 는 공백 뒤(한글도 어절), Character 는 글자 사이 어디서나(D-200 (5)).
    enum class TextWrapMode : std::uint8_t { Word, Character };
    // 블록의 어느 점이 오브젝트 원점인가.
    enum class TextAlignX : std::uint8_t { Left, Center, Right };
    enum class TextAlignY : std::uint8_t { Top, Middle, Baseline, Bottom };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::TextOverflow, "Component::TextOverflow",
        { Component::TextOverflow::Overflow, "Overflow" },
        { Component::TextOverflow::Wrap,     "Wrap" },
        { Component::TextOverflow::Clip,     "Clip" });
    JBRO_DEFINE_ENUM_TYPE(Component::TextWrapMode, "Component::TextWrapMode",
        { Component::TextWrapMode::Word,      "Word" },
        { Component::TextWrapMode::Character, "Character" });
    JBRO_DEFINE_ENUM_TYPE(Component::TextAlignX, "Component::TextAlignX",
        { Component::TextAlignX::Left,   "Left" },
        { Component::TextAlignX::Center, "Center" },
        { Component::TextAlignX::Right,  "Right" });
    JBRO_DEFINE_ENUM_TYPE(Component::TextAlignY, "Component::TextAlignY",
        { Component::TextAlignY::Top,      "Top" },
        { Component::TextAlignY::Middle,   "Middle" },
        { Component::TextAlignY::Baseline, "Baseline" },
        { Component::TextAlignY::Bottom,   "Bottom" });
}
