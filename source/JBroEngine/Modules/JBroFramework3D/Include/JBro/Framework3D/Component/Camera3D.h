#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Camera3D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Camera3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

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
        JBRO_FIELD(Float, verticalFieldOfView, Range(1.0f, 179.0f)) = 60.0f;
        // 직교 투영의 세로 절반 크기(월드 단위). 2D 와 같은 뜻이다.
        JBRO_FIELD(Float, orthographicSize) = 10.0f;
        JBRO_FIELD(Float, nearPlane) = 0.1f;
        JBRO_FIELD(Float, farPlane)  = 1000.0f;
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        JBRO_FIELD(Bool,  primary) = false;
    };
}
