#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    class Camera3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Camera3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        float verticalFieldOfView = 60.0f;
    };
}
