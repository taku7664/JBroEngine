#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/TextOptions.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

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
        // 게임 문자열 표의 키다(D-226). 비어 있지 않으면 `text` 대신 이 키의 글자다: 지금 로케일 → 폴백 로케일 → 키 그대로.
        JBRO_FIELD(TextId, textKey);
        JBRO_FIELD(AssetId, fontId);
        JBRO_FIELD(AssetHandle, font, NoSerialize() | ReadOnly() | Tooltip("fontId 에서 해석된 값"));
        JBRO_FIELD(Float, fontSize, Range(1, 512)) = 32.0f;
        // 글자 픽셀이다. 0 이면 그 방향으로 제한이 없다.
        JBRO_FIELD(Vector2, boxSize);
        JBRO_FIELD(TextOverflow, overflow) = TextOverflow::Wrap;
        JBRO_FIELD(TextWrapMode, wrapMode) = TextWrapMode::Word;
        JBRO_FIELD(TextAlignX, alignX) = TextAlignX::Left;
        JBRO_FIELD(TextAlignY, alignY) = TextAlignY::Baseline;
        JBRO_FIELD(Float, lineSpacing, Range(0, 10)) = 1.0f;
        JBRO_FIELD(Float, letterSpacing) = 0.0f;
        JBRO_FIELD(Color, color) = { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Int32, renderOrder) = 0;
        JBRO_FIELD(Bool, visible) = true;
        // 외곽선이다(폰트가 `Sdf` 일 때만, 4 단계). 폭은 **글자 픽셀**이라 카메라를 빼도 글자와 외곽선의 비가 같다. 폰트의 퍼짐보다
        // 굵게 주면 퍼짐까지로 자른다 - 넘으면 글자마다 네모가 칠해지던 기존 엔진의 결함(text-plan §1.2 의 7 번)을 막는다.
        JBRO_FIELD(Color, outlineColor) = { 0.0f, 0.0f, 0.0f, 1.0f };
        JBRO_FIELD(Float, outlineWidth, Range(0, 64)) = 0.0f;
        // **자동 크기**다. 켜면 `fontSize` 대신 [min, max] 에서 상자(`boxSize`)에 들어가는 가장 큰 크기를 쓴다 - 상자가 없으면 뜻이 없다.
        // 비트맵은 정수 크기, SDF 는 0.25 픽셀까지 맞춘다.
        JBRO_FIELD(Bool, autoSize) = false;
        JBRO_FIELD(Float, minFontSize, Range(1, 512)) = 8.0f;
        JBRO_FIELD(Float, maxFontSize, Range(1, 512)) = 72.0f;
        // **픽셀 맞춤**이다. 켜면 글리프 원점을 글자 픽셀의 정수 자리로 반올림한다 - 가운데 정렬·커닝이 만든 소수 자리 때문에 비트맵
        // 글자가 텍셀 사이를 샘플해 흐려지는 것을 막는다. 화면 픽셀과 맞으려면 오브젝트 위치·PPU·카메라도 정수 픽셀이어야 한다
        // (그것은 이 필드가 맞추지 않는다).
        JBRO_FIELD(Bool, pixelSnap) = false;
        // **리치 텍스트**다(D-221). 켜면 글자 속의 `<color=#RRGGBB>`·`<color=#RRGGBBAA>` ... `</color>` 와 `<size=픽셀>` ... `</size>` 를
        // 태그로 읽는다. `<` 자체는 `<<` 로 쓴다. 태그 색은 RGB 와 알파를 정하고 `color` 의 알파를 곱한다.
        JBRO_FIELD(Bool, richText) = false;
    };
}
