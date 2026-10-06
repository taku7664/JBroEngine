#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/TextOptions.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // 글자 판이 어디를 보나(D-222). Transform 은 오브젝트의 회전을 따르는 판(간판·벽의 글씨), Billboard 는 늘 카메라를 보는 판(이름표)이다.
    // Billboard 는 오브젝트의 위치와 크기만 쓰고 회전은 뷰마다 카메라의 것으로 바꾼다 - 게임 뷰와 편집 뷰가 각자 제 카메라를 본다.
    enum class TextFacing3D : std::uint8_t { Transform, Billboard };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::TextFacing3D, "Component::TextFacing3D",
        { Component::TextFacing3D::Transform, "Transform" },
        { Component::TextFacing3D::Billboard, "Billboard" });
}

namespace JBro::Component
{
    // 3D 월드의 텍스트다(D-222). `Text2D` 와 같은 커널·아틀라스·레이아웃 캐시(JBroTextRendering)를 쓰고, 글자마다 월드 텍스트 사각형으로
    // 그린다 - 메시에 가려지고 글자끼리는 가리지 않는다. 크기와 상자는 **글자 픽셀**이고 유닛은 폰트 에셋의 `pixelsPerUnit` 으로 나눈다.
    // 판은 오브젝트 로컬의 XY 평면이고 +Z 쪽에서 읽힌다(카메라가 -Z 를 보므로 기본 자세에서 정면이다).
    //
    // 글자는 컴포넌트에 없다(D-51): `text` 는 호스트 `TextStore` 의 번호다. 스크립트는 `TextStore` 로 바꾼다(스크립트 DLL 에 호스트의
    // 저장소가 묶여 있다, ABI 5).
    class Text3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Text3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 떼이면(오브젝트 파괴·캔버스 내리기 포함) 제 글자 칸을 돌려준다. 컴포넌트는 호스트의 캔버스만 떼므로 호스트의 저장소다.
        void OnDetached() override
        {
            TextStore::Get().Destroy(text);
            text = {};
            TextStore::Get().Destroy(textKey);
            textKey = {};
            ComponentBase::OnDetached();
        }

        JBRO_REFLECT_BODY(Text3D)

        JBRO_FIELD(TextId, text);
        // 게임 문자열 표의 키다(D-226). 비어 있지 않으면 `text` 대신 이 키의 글자다: 지금 로케일 → 폴백 로케일 → 키 그대로.
        JBRO_FIELD(TextId, textKey);
        JBRO_FIELD(AssetId, fontId);
        JBRO_FIELD(AssetHandle, font, NoSerialize() | ReadOnly() | Tooltip("fontId 에서 해석된 값"));
        JBRO_FIELD(Float, fontSize, Range(1.0f, 512.0f)) = 32.0f;
        // 글자 픽셀이다. 0 이면 그 방향으로 제한이 없다.
        JBRO_FIELD(Float, boxWidth, Range(0.0f, 100000.0f)) = 0.0f;
        JBRO_FIELD(Float, boxHeight, Range(0.0f, 100000.0f)) = 0.0f;
        JBRO_FIELD(TextOverflow, overflow) = TextOverflow::Wrap;
        JBRO_FIELD(TextWrapMode, wrapMode) = TextWrapMode::Word;
        // 3D 의 글자는 대개 오브젝트 자리에 가운데로 선다.
        JBRO_FIELD(TextAlignX, alignX) = TextAlignX::Center;
        JBRO_FIELD(TextAlignY, alignY) = TextAlignY::Middle;
        JBRO_FIELD(Float, lineSpacing, Range(0.0f, 10.0f)) = 1.0f;
        JBRO_FIELD(Float, letterSpacing) = 0.0f;
        JBRO_FIELD(Color, color) = { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Bool, visible) = true;
        JBRO_FIELD(TextFacing3D, facing) = TextFacing3D::Transform;
        // 외곽선이다(폰트가 `Sdf` 일 때만). 폭은 글자 픽셀이다 - `Text2D` 와 같다.
        JBRO_FIELD(Color, outlineColor) = { 0.0f, 0.0f, 0.0f, 1.0f };
        JBRO_FIELD(Float, outlineWidth, Range(0.0f, 64.0f)) = 0.0f;
        // **리치 텍스트**다(D-221). `Text2D::richText` 와 같은 태그를 읽는다.
        JBRO_FIELD(Bool, richText) = false;
    };
}
