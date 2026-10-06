#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // 그림자의 모양을 어디서 가져오는가(D-291). 값의 차례는 파일이 쓰므로 뒤에만 더한다.
    enum class ShadowShape2D : std::uint8_t
    {
        // 같은 오브젝트의 `Collider2D` 다(상자·원·캡슐·폴리곤·체인). 콜라이더가 없으면 그림자가 없다.
        Collider,
        // 같은 오브젝트의 `SpriteRenderer2D` 가 그려지는 사각형이다. 투명한 픽셀의 모양은 따르지 않는다.
        Sprite,
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::ShadowShape2D, "Component::ShadowShape2D",
        { Component::ShadowShape2D::Collider, "Collider" },
        { Component::ShadowShape2D::Sprite,   "Sprite" });
}

namespace JBro::Component
{
    // **2D 그림자를 드리운다**(D-291, tasks/lighting2d-plan.md 3 단계). 그림자를 드리우는 라이트(`Light2D::castShadows`)의 빛을 이 모양이 가린다.
    // 모양의 변을 라이트에서 밀어내 그리므로 화면 밖에 있어도, 라이트가 멀리 있어도 그림자가 맞다. 모양의 안쪽은 기본으로 밝다 - 벽 스프라이트가
    // 제 그림자에 묻히지 않는다. 감춘 레이어와 화면 레이어의 것은 그림자를 드리우지 않는다.
    class ShadowCaster2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ShadowCaster2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ShadowCaster2D)

        JBRO_FIELD(ShadowShape2D, shape) = ShadowShape2D::Collider;
        // 참이면 모양의 안쪽도 제 그림자에 든다(라이트를 향한 변도 밀어낸다). 체인 콜라이더는 두께가 없어 늘 이렇다.
        JBRO_FIELD(Bool, selfShadow) = false;
    };
}
