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

        // 골격이다(D-53). 2D 쪽에 있는 월드 캐시가 여기에는 아직 없고,
        // 그것이 생기면 저작 값과 같은 방식으로 NoSerialize | ReadOnly 가 붙는다.
        JBRO_FIELD(JBro::Vec3,       position);
        JBRO_FIELD(JBro::Quaternion, rotation);
        JBRO_FIELD(JBro::Vec3,       scale) { 1.0f, 1.0f, 1.0f };
    };
}
