#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        bool Positive(float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }

        // 원본 1 픽셀 격자에 맞춘다. 반올림이라 카메라가 격자 사이를 지날 때 가까운 쪽으로 붙는다.
        float SnapToPixel(float value, float pixelsPerUnit)
        {
            return static_cast<float>(std::round(static_cast<double>(value) * pixelsPerUnit) / pixelsPerUnit);
        }
    }

    bool IsDrawableCamera2D(const RenderCamera2D& camera)
    {
        if (false == std::isfinite(camera.nearPlane) || false == std::isfinite(camera.farPlane)
            || camera.nearPlane >= camera.farPlane)
        {
            return false;
        }
        switch (camera.projection)
        {
        case Component::CameraProjection2D::Orthographic:
            return Positive(camera.orthographicSize);
        case Component::CameraProjection2D::PixelPerfect:
            return Positive(camera.pixelsPerUnit);
        default:
            return false;
        }
    }

    void ApplyCameraArea(const RenderCamera2D* camera, ScreenSpaceFrame& frame)
    {
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (camera == nullptr || camera->projection != Component::CameraProjection2D::PixelPerfect
            || false == IsDrawableCamera2D(*camera))
        {
            return;
        }
        ScreenArea area;
        float scale = 0.0f;
        if (ComputePixelPerfectArea(frame, area, scale))
        {
            frame.areaX = area.x;
            frame.areaY = area.y;
            frame.areaWidth = area.width;
            frame.areaHeight = area.height;
        }
    }

    bool ComputeCameraView2D(const RenderCamera2D& camera, const ScreenSpaceFrame& frame, CameraView2D& result)
    {
        ScreenArea area;
        if (false == IsDrawableCamera2D(camera) || false == GetScreenArea(frame, area))
        {
            return false;
        }
        CameraView2D view;
        view.view = camera.view;
        if (camera.projection == Component::CameraProjection2D::PixelPerfect)
        {
            // 기준 해상도의 1 픽셀이 원본 1 픽셀이다. 화면에서는 레터박스 사각형이 그것을 정수 배율로 키운다.
            view.halfWidth = frame.referenceWidth * 0.5f / camera.pixelsPerUnit;
            view.halfHeight = frame.referenceHeight * 0.5f / camera.pixelsPerUnit;
            // 뷰 공간에서 맞춘다 - 화면의 픽셀 격자가 뷰 공간의 축이다. 월드에서 카메라 위치를 맞추면 돌린 카메라에서 어긋난다.
            view.view.m31 = SnapToPixel(camera.view.m31, camera.pixelsPerUnit);
            view.view.m32 = SnapToPixel(camera.view.m32, camera.pixelsPerUnit);
        }
        else
        {
            view.halfHeight = camera.orthographicSize;
            view.halfWidth = camera.orthographicSize * area.width / area.height;
        }
        if (false == Positive(view.halfWidth) || false == Positive(view.halfHeight)
            || false == std::isfinite(view.view.m31) || false == std::isfinite(view.view.m32))
        {
            return false;
        }
        result = view;
        return true;
    }
}
