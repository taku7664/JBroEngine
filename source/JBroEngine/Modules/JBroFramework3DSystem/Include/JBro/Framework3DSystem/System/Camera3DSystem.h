#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/Viewport.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class RenderWorld3D;
    struct RenderCamera3D;
}

namespace JBro::Component
{
    class Camera3D;
}

namespace JBro::System
{
    // **캔버스의 뷰포트 목록을 차례로 뷰로 렌더 월드에 뜬다**(D-438, viewport-plan §4.4). 주 카메라는 화면에 그리는 첫 뷰다(2D 와 같다).
    class Camera3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;
        void SetRenderWorld(RenderWorld3D* renderWorld);
        void ExtractRenderWorld(Canvas& canvas);
        // **뷰포트가 그리는 카메라 컴포넌트**(D-438). 번호가 있으면 그 오브젝트의 켜진 `Camera3D`, 없으면(자동) 캔버스 차례의 첫 켜진 카메라다.
        // 켜진 카메라는 트랜스폼까지 켜진 것이다. 못 찾으면 널이다.
        static Component::Camera3D* ResolveViewportCamera(Canvas& canvas, const CanvasViewport& viewport);
        // 켜진 카메라 수다. 자동 뷰포트가 애매한지 인스펙터가 본다.
        static UInt32 CountActiveCameras(Canvas& canvas);
        // **주 카메라**의 뷰를 채운다(D-438): 화면에 그리는 첫 켜진 뷰포트. 없으면 거짓이다. 오디오 리스너가 없을 때의 자리도 이것이다.
        static Bool SelectCamera(Canvas& canvas, RenderCamera3D& camera);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        static Bool FillView(Canvas& canvas, const CanvasViewport& viewport, RenderCamera3D& item);

        RenderWorld3D* m_renderWorld = nullptr;
        // 3D 렌더 텍스처 뷰포트는 아직 그리지 않는다(viewport-plan §6). 한 번만 경고한다.
        Bool m_warnedRenderTexture = false;
    };
}
