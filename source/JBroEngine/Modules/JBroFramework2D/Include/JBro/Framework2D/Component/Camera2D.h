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
        JBRO_FIELD(Color, clearColor) { 0.08f, 0.09f, 0.11f, 1.0f };
        JBRO_FIELD(Bool,  primary) = false;
        // **뷰포트**(D-359, 분할 화면). 대상의 어느 사각형에 그리는가 - 왼쪽 위 원점의 0..1 비율이다. 기본은 전체다. 켜진 카메라마다 제 뷰포트에 한 번씩 그리고
        // `depth` 가 작은 것부터다(같으면 계층 차례). 화면 레이어·앵커는 뷰포트와 무관하게 대상 전체다. `PixelPerfect` 의 레터박스는 뷰포트 안에서 센다.
        // 포인터는 그 픽셀이 든 뷰포트의 카메라(겹치면 `depth` 가 큰 것)로 역투영한다. 비율이 0 이거나 뷰포트가 비면 그 카메라는 그리지 않는다.
        JBRO_FIELD(Vector2, viewportOrigin, Range(0.0f, 1.0f));
        JBRO_FIELD(Vector2, viewportSize, Range(0.0f, 1.0f)) { 1.0f, 1.0f };
        JBRO_FIELD(Int32, depth) = 0;
        // 앞 카메라가 이미 그린 대상에 그릴 때 제 뷰포트를 `clearColor` 로 먼저 칠하는가(D-362). 첫 카메라는 언제나 대상 전체를 지운다. 끄면 앞 그림 위에 겹쳐 그린다.
        JBRO_FIELD(Bool, clearViewport) = true;
        // **이 카메라의 후처리 프로필**(`.jpost`, D-344). 비어 있으면 후처리가 없다. 게임 화면과 캔버스 뷰(단추로 끌 수 있다)가 이 프로필을 건다.
        JBRO_FIELD(AssetId, postProcessProfileId);
        JBRO_FIELD(AssetHandle, postProcessProfile, NoSerialize() | ReadOnly() | Hidden());
        // **렌더 텍스처**(`.jrt`, D-362). 있으면 이 카메라는 화면 대신 그 텍스처에 그린다(뷰포트는 텍스처 안의 비율) - 스프라이트의 `renderTextureId` 가 같은 에셋을 보이면
        // 미니맵·초상화가 된다. 그런 카메라는 주 카메라·포인터의 카메라가 되지 않고, 화면의 카메라보다 먼저 그린다.
        JBRO_FIELD(AssetId, renderTextureId);
        JBRO_FIELD(AssetHandle, renderTexture, NoSerialize() | ReadOnly() | Tooltip("renderTextureId 에서 해석된 값") | Hidden());
    };
}
