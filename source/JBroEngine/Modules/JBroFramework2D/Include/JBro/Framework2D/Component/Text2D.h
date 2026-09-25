#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/TextStore.h>

#include <cstdint>

namespace JBro::Component
{
    // 상자를 넘는 줄을 어떻게 하나(text-plan §4.2). Wrap 과 Clip 은 `boxSize.x` 에서 줄을 바꾸고, Clip 은 `boxSize.y` 밖의 줄을 버린다.
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

namespace JBro::Component
{
    // 월드 공간 2D 텍스트다(D-200, text-plan §4.2). **저작 값만 든다** - 레이아웃과 경계는 텍스트 시스템의 캐시가 든다.
    //
    // 글자는 컴포넌트에 없다(D-51): `text` 는 호스트 `TextStore` 의 번호이고, 파일·인스펙터·복사에는 글자로 오간다(TextStore.h).
    // 스크립트는 `Text2DService::SetText` 로 바꾼다. 크기와 상자는 **글자 픽셀**이고, 유닛은 폰트 에셋의 `pixelsPerUnit` 으로 나눈다
    // (스프라이트의 PPU 와 같은 규칙, D-119).
    class Text2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Text2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 떼이면(오브젝트 파괴·캔버스 내리기 포함) 제 글자 칸을 돌려준다.
        void OnDetached() override;

        JBRO_REFLECT_BODY(Text2D)

        JBRO_FIELD(TextId, text);
        JBRO_FIELD(AssetId, fontId);
        JBRO_FIELD(AssetHandle, font, NoSerialize() | ReadOnly() | Tooltip("fontId 에서 해석된 값"));
        JBRO_FIELD(float, fontSize, Range(1, 512)) = 32.0f;
        // 글자 픽셀이다. 0 이면 그 방향으로 제한이 없다.
        JBRO_FIELD(Vec2, boxSize);
        JBRO_FIELD(TextOverflow, overflow) = TextOverflow::Wrap;
        JBRO_FIELD(TextWrapMode, wrapMode) = TextWrapMode::Word;
        JBRO_FIELD(TextAlignX, alignX) = TextAlignX::Left;
        JBRO_FIELD(TextAlignY, alignY) = TextAlignY::Baseline;
        JBRO_FIELD(float, lineSpacing, Range(0, 10)) = 1.0f;
        JBRO_FIELD(float, letterSpacing) = 0.0f;
        JBRO_FIELD(Color, color) = { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(std::int32_t, renderOrder) = 0;
        JBRO_FIELD(bool, visible) = true;
    };
}
