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

    namespace
    {
        Float Unit(Float value)
        {
            return Float(std::isfinite(value.Get()) ? Float::Clamp(value, 0.0f, 1.0f) : 0.0f);
        }
    }

    Bool HasDrawableViewport(const RenderCamera2D& camera)
    {
        const Float width = Unit(camera.viewportSize.x);
        const Float height = Unit(camera.viewportSize.y);
        return width > 0.0f && height > 0.0f && Unit(camera.viewportOrigin.x) < 1.0f && Unit(camera.viewportOrigin.y) < 1.0f;
    }

    Bool HasFullViewport(const RenderCamera2D& camera)
    {
        return HasDrawableViewport(camera) && Unit(camera.viewportOrigin.x) <= 0.0f && Unit(camera.viewportOrigin.y) <= 0.0f
            && Unit(camera.viewportSize.x) >= 1.0f && Unit(camera.viewportSize.y) >= 1.0f;
    }

    void ApplyCameraViewport(const RenderCamera2D& camera, ScreenSpaceFrame& frame)
    {
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (false == IsDrawableCamera2D(camera) || false == HasDrawableViewport(camera) || false == (frame.targetWidth > 0.0f) || false == (frame.targetHeight > 0.0f))
        {
            return;
        }
        const Bool full = HasFullViewport(camera);
        // 뷰포트의 픽셀 사각형. 오른쪽·아래로 넘치는 만큼 자른다.
        const Float originX = Unit(camera.viewportOrigin.x);
        const Float originY = Unit(camera.viewportOrigin.y);
        const Float width = Float(Unit(camera.viewportSize.x) + originX > 1.0f ? (Float(1.0f) - originX).Get() : Unit(camera.viewportSize.x).Get());
        const Float height = Float(Unit(camera.viewportSize.y) + originY > 1.0f ? (Float(1.0f) - originY).Get() : Unit(camera.viewportSize.y).Get());
        const Float pixelX = originX * frame.targetWidth;
        const Float pixelY = originY * frame.targetHeight;
        const Float pixelWidth = width * frame.targetWidth;
        const Float pixelHeight = height * frame.targetHeight;
        if (camera.projection == Component::CameraProjection2D::PixelPerfect)
        {
            // 레터박스는 뷰포트 안에서 센다 - 뷰포트 크기를 대상으로 두고 잰 뒤 뷰포트 자리를 더한다.
            ScreenSpaceFrame inside = frame;
            inside.targetWidth = pixelWidth;
            inside.targetHeight = pixelHeight;
            inside.areaX = 0.0f;
            inside.areaY = 0.0f;
            inside.areaWidth = 0.0f;
            inside.areaHeight = 0.0f;
            ScreenArea area;
            Float scale = 0.0f;
            if (ComputePixelPerfectArea(inside, area, scale))
            {
                frame.areaX = pixelX + area.x;
                frame.areaY = pixelY + area.y;
                frame.areaWidth = area.width;
                frame.areaHeight = area.height;
                return;
            }
        }
        if (false == full)
        {
            frame.areaX = pixelX;
            frame.areaY = pixelY;
            frame.areaWidth = pixelWidth;
            frame.areaHeight = pixelHeight;
        }
    }

    void ApplyCameraArea(const RenderCamera2D* camera, ScreenSpaceFrame& frame)
    {
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (camera == nullptr || false == HasFullViewport(*camera))
        {
            return;
        }
        ApplyCameraViewport(*camera, frame);
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

}
