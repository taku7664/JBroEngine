#pragma once

#include <JBro/Framework3D/Math3DReflection.h>
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

        JBRO_REFLECT_BODY(Camera3D)

        // 골격이다(D-53). 근평면·원평면·투영 방식이 아직 없다.
        JBRO_FIELD(float, verticalFieldOfView, Range(1, 179)) = 60.0f;
    };
}
