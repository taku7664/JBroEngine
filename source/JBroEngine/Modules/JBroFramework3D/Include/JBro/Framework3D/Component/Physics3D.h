#pragma once

#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    class Rigidbody3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Rigidbody3D)

        // 시뮬레이션이 매 프레임 다시 쓴다. 저장하면 씬을 열 때마다
        // 물체가 저장된 순간의 속도로 튀어 나간다(2D 쪽과 같은 이유).
        JBRO_FIELD(JBro::Vector3, velocity, NoSerialize());
        JBRO_FIELD(Float,      mass, Range(0.0f, 1000.0f)) = 1.0f;
    };

    class Collider3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Collider3D)

        // 골격이다(D-53). 2D 쪽에 있는 shape·offset·isTrigger 가 아직 없다.
        JBRO_FIELD(JBro::Vector3, size) { 1.0f, 1.0f, 1.0f };
    };
}
