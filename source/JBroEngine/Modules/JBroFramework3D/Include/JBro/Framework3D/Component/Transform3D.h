#pragma once

#include <JBro/Framework3D/Math3DReflection.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    class Transform3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Transform3D)

        JBRO_FIELD(JBro::Vec3,       position);
        JBRO_FIELD(JBro::Quaternion, rotation);
        JBRO_FIELD(JBro::Vec3,       scale) { 1.0f, 1.0f, 1.0f };

        // 월드 캐시다. `Transform3DSystem` 이 매 프레임 채우고 저장하지 않는다(2D 와 같은 모양).
        // **행렬이 아니라 분해된 값이다**(framework3d-plan §2.1) - `Matrix4x4` 는 `JBroGraphics` 소유라
        // 컴포넌트 라이브러리가 들 수 없고, 스크립트 프렐류드에 렌더러 타입이 새면 안 된다. 행렬은
        // 렌더 월드를 뜨는 시스템이 만든다. 대가: 비균등 스케일 아래의 회전이 만드는 전단은 자식에게
        // 전해지지 않는다.
        JBRO_FIELD(JBro::Vec3,       worldPosition, NoSerialize() | ReadOnly() | Category("World cache"));
        JBRO_FIELD(JBro::Quaternion, worldRotation, NoSerialize() | ReadOnly() | Category("World cache"));
        JBRO_FIELD(JBro::Vec3,       worldScale,    NoSerialize() | ReadOnly() | Category("World cache")) { 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(bool,             worldValid,    NoSerialize() | ReadOnly() | Category("World cache")) = false;
    };
}
