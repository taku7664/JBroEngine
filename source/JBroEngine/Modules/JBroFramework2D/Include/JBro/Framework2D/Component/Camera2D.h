#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    enum class CameraProjection2D { Orthographic, PixelPerfect };

    class Camera2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Camera2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        CameraProjection2D projection = CameraProjection2D::Orthographic;
        float orthographicSize = 10.0f;
        float nearPlane = -100.0f;
        float farPlane  =  100.0f;
        Color clearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
        bool  primary = false;
    };
}
