#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>

#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    namespace
    {
        Bool Positive(Float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }

        // 원본 1 픽셀 격자에 맞춘다. 반올림이라 카메라가 격자 사이를 지날 때 가까운 쪽으로 붙는다.
        Float SnapToPixel(Float value, Float pixelsPerUnit)
        {
            return static_cast<JBro::Float>(std::round(static_cast<double>(value) * pixelsPerUnit) / pixelsPerUnit);
        }
    }

    Bool IsDrawableCamera2D(const RenderCamera2D& camera)
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
        Float scale = 0.0f;
        if (ComputePixelPerfectArea(frame, area, scale))
        {
            frame.areaX = area.x;
            frame.areaY = area.y;
            frame.areaWidth = area.width;
            frame.areaHeight = area.height;
        }
    }

    Bool ComputeCameraView2D(const RenderCamera2D& camera, const ScreenSpaceFrame& frame, CameraView2D& result)
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

    Bool ComputeCameraWorldArea2D(const RenderCamera2D& camera, const Matrix3x2& world, const ScreenSpaceFrame& frame,
        Vector2 (&corners)[4])
    {
        CameraView2D view;
        if (false == ComputeCameraView2D(camera, frame, view))
        {
            return false;
        }
        const Float xs[4] = {-view.halfWidth, view.halfWidth, view.halfWidth, -view.halfWidth};
        const Float ys[4] = {-view.halfHeight, -view.halfHeight, view.halfHeight, view.halfHeight};
        for (std::size_t index = 0; index < 4; ++index)
        {
            const Float x = xs[index];
            const Float y = ys[index];
            corners[index] = {x * world.m11 + y * world.m21 + world.m31, x * world.m12 + y * world.m22 + world.m32};
        }
        return true;
    }

    Bool ComputeParallaxOffset2D(const Matrix3x2& view, Float factor, Float& dx, Float& dy)
    {
        // 카메라의 월드 자리는 뷰가 원점으로 보내는 점이다: x·m11 + y·m21 + m31 = 0, x·m12 + y·m22 + m32 = 0.
        const Float det = view.m11 * view.m22 - view.m21 * view.m12;
        if (false == std::isfinite(det) || std::fabs(det) < 1e-12f)
        {
            return false;
        }
        const Float cameraX = (view.m21 * view.m32 - view.m31 * view.m22) / det;
        const Float cameraY = (view.m31 * view.m12 - view.m11 * view.m32) / det;
        dx = cameraX * (1.0f - factor);
        dy = cameraY * (1.0f - factor);
        return std::isfinite(dx) && std::isfinite(dy);
    }
}
