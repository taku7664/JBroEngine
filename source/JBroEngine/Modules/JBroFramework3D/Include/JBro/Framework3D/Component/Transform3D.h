#pragma once

#include <JBro/Framework3D/Math3D.h>
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

        JBro::Vec3 position;
        JBro::Quaternion rotation;
        JBro::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };
}
