#pragma once

#include <JBro/Host/IFramework.h>

namespace JBro
{
    class RenderWorld3D;
    class Renderer;
    struct RenderCamera3D;
    struct CameraParams;
    struct Extent2D;

    namespace Internal
    {
        // 카메라 값을 렌더러의 `CameraParams`(뷰·투영·뷰포트·클리어 색)로. 값이 말이 안 되면 거짓.
        bool BuildCamera3D(const RenderCamera3D& source, const Extent2D& extent, CameraParams& result);
        // 렌더 월드를 뷰 하나로 렌더러에 넘긴다. 2D 의 `SubmitRenderWorld2D` 와 같은 계약이다.
        RenderResult SubmitRenderWorld3D(const RenderWorld3D& world, Renderer& renderer);
    }
}
