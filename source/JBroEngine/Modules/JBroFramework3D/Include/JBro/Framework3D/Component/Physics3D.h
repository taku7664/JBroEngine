#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/Runtime/Component.h>

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

        JBro::Vec3 velocity;
        float mass = 1.0f;
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

        JBro::Vec3 size{ 1.0f, 1.0f, 1.0f };
    };
}
