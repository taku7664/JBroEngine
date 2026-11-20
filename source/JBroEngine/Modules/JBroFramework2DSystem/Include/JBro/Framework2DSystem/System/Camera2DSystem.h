#pragma once

#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/Viewport.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    class Camera2D;
}

namespace JBro::System
{
    class Camera2DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // 뷰포트의 렌더 텍스처를 푼 핸들이다(D-438). 뷰포트 차례이고, 프레임워크의 `BindCanvasAssets` 가 채운다. 없으면 렌더 텍스처 뷰포트를 그리지 않는다.
        void SetViewportTextures(ArrayView<const AssetHandle> textures);
        // Caller begins/ends the frame; transforms must be updated before extraction.
        // **캔버스의 뷰포트 목록을 차례로 뷰로 옮긴다**(D-438). 주 카메라(`RenderWorld2D::SetCamera`)는 화면에 그리는 첫 뷰다.
        void ExtractRenderWorld(Canvas& canvas);
        // **주 카메라**를 고른다(D-438): 화면에 그리는 첫 켜진 뷰포트의 뷰다. 버튼의 역투영도 이것을 부른다(D-237) -
        // 그리는 카메라와 누르는 카메라가 달라지지 않는다.
        // **값이 잘못된 카메라는 건너뛴다**(D-239, `IsDrawableCamera2D`). `unusable` 을 주면 그렇게 건너뛴 수를 더한다.
        static Bool SelectCamera(Canvas& canvas, RenderCamera2D& camera, UInt32* unusable = nullptr);
        // **그 픽셀이 든 뷰포트의 뷰**(D-359, D-438)다. 겹치면 목록에서 뒤의 것(나중에 그려 위에 보이는 것). 드는 뷰포트가 없으면 `SelectCamera` 다. `frame` 의 사각형은 비워서 준다.
        static Bool SelectCameraAt(Canvas& canvas, const ScreenSpaceFrame& frame, Float pixelX, Float pixelY, RenderCamera2D& camera);
        // **뷰포트가 그리는 카메라 컴포넌트**(D-438). 번호가 있으면 그 오브젝트의 켜진 `Camera2D`, 없으면(자동) 캔버스 차례의 첫 켜진 카메라다.
        // 켜진 카메라는 트랜스폼이 켜져 있고 월드 행렬이 뒤집히는 것이다. 못 찾으면 널이다.
        static Component::Camera2D* ResolveViewportCamera(Canvas& canvas, const CanvasViewport& viewport);
        // 켜진 카메라 수다(트랜스폼까지 켜진 것). 자동 뷰포트가 애매한지 인스펙터가 본다.
        static UInt32 CountActiveCameras(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        // 뷰포트 하나를 뷰로 채운다. 꺼졌거나 카메라를 못 찾거나 그릴 수 없으면 거짓이다. 값이 잘못된 카메라면 `unusable` 을 하나 더한다.
        static Bool FillView(Canvas& canvas, const CanvasViewport& viewport, RenderCamera2D& item, UInt32* unusable);

        RenderWorld2D* m_renderWorld = nullptr;
        ArrayView<const AssetHandle> m_viewportTextures;
        // 지난 프레임에 건너뛴 카메라 수다. 늘 때만 경고한다 - 매 프레임 쓰면 로그가 그것으로 찬다.
        UInt32 m_warnedUnusable = 0;
    };
}
