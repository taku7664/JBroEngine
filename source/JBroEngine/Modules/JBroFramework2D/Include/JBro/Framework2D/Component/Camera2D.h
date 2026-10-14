#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Camera2D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Camera2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

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
        // `Orthographic` 의 화면 세로 절반이 담는 월드 길이다. `PixelPerfect` 에서는 쓰지 않는다(D-239).
        JBRO_FIELD(Float, orthographicSize) = 10.0f;
        // `PixelPerfect` 에서 원본 몇 픽셀이 1 유닛인가(D-239). 보이는 범위는 프로젝트의 기준 해상도 / 이 값이다.
        // 스프라이트 에셋의 기본 PPU(100)와 같게 시작한다 - 둘이 다르면 원본 1 픽셀이 화면 픽셀에 딱 맞지 않는다.
        JBRO_FIELD(Float, pixelsPerUnit) = 100.0f;
        JBRO_FIELD(Float, nearPlane) = -100.0f;
        JBRO_FIELD(Float, farPlane)  =  100.0f;
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        JBRO_FIELD(Bool,  primary) = false;
    };
}
