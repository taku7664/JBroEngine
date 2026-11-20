#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Camera2D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Camera2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
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
        // 그 뷰포트를 이 카메라로 그릴 때 먼저 칠하는 색이다. 대상의 첫 뷰는 대상 전체를, 뒤의 뷰는 제 사각형을(뷰포트의 `clearViewport`) 칠한다.
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        // **화면 어디에·어떤 레이어를·어떤 차례로 그릴지는 카메라가 아니라 캔버스의 뷰포트 목록이 정한다**(D-438, viewport-plan).
        // 그래서 여기에는 `primary`·뷰포트 사각형·`depth`·렌더 텍스처가 없다 - 카메라는 월드 속의 눈이다.
        // **이 카메라의 후처리 프로필**(`.jpost`, D-344). 비어 있으면 후처리가 없다. 게임 화면과 캔버스 뷰(단추로 끌 수 있다)가 이 프로필을 건다.
        JBRO_FIELD(AssetId, postProcessProfileId);
        JBRO_FIELD(AssetHandle, postProcessProfile, NoSerialize() | ReadOnly() | Hidden());
    };
}
