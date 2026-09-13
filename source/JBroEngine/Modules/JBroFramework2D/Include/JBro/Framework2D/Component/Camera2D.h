#pragma once

#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    enum class CameraProjection2D { Orthographic, PixelPerfect };
}

namespace JBro
{
    // 저장 파일에는 이름이 적힌다. 숫자로 적으면 나중에 값을 가운데 끼워 넣는 순간
    // 예전 파일이 전부 한 칸씩 밀린다.
    JBRO_DEFINE_ENUM_TYPE(Component::CameraProjection2D, "Component::CameraProjection2D",
        { Component::CameraProjection2D::Orthographic, "Orthographic" },
        { Component::CameraProjection2D::PixelPerfect, "PixelPerfect" });
}

namespace JBro::Component
{
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

        JBRO_REFLECT_BODY(Camera2D)

        JBRO_FIELD(CameraProjection2D, projection) = CameraProjection2D::Orthographic;
        JBRO_FIELD(float, orthographicSize) = 10.0f;
        JBRO_FIELD(float, nearPlane) = -100.0f;
        JBRO_FIELD(float, farPlane)  =  100.0f;
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        JBRO_FIELD(bool,  primary) = false;
    };
}
