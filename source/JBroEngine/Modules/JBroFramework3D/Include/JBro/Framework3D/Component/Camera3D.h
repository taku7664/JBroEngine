#pragma once

#include <JBro/Framework3D/Math3DReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    enum class CameraProjection3D { Perspective, Orthographic };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::CameraProjection3D, "Component::CameraProjection3D",
        { Component::CameraProjection3D::Perspective,  "Perspective" },
        { Component::CameraProjection3D::Orthographic, "Orthographic" });
}

namespace JBro::Component
{
    // 3D 카메라다. 이름은 2D 의 `Camera2D` 와 맞춘다 - 같은 뜻의 값은 같은 이름이어야 인스펙터에서
    // 두 차원이 나란히 읽힌다. 카메라는 자기 `Transform3D` 의 -Z 를 본다(framework3d-plan §2.2).
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

        JBRO_FIELD(CameraProjection3D, projection) = CameraProjection3D::Perspective;
        // 세로 시야각(도). 원근 투영에서만 쓴다.
        JBRO_FIELD(float, verticalFieldOfView, Range(1, 179)) = 60.0f;
        // 직교 투영의 세로 절반 크기(월드 단위). 2D 와 같은 뜻이다.
        JBRO_FIELD(float, orthographicSize) = 10.0f;
        JBRO_FIELD(float, nearPlane) = 0.1f;
        JBRO_FIELD(float, farPlane)  = 1000.0f;
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        JBRO_FIELD(bool,  primary) = false;
    };
}
