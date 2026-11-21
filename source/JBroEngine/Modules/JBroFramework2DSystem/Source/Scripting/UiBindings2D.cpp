#include <JBro/Framework2DSystem/Scripting/UiBindings2D.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/ScreenUi/Rendering/Parallax2D.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        Bool Apply(const Matrix3x2& m, Float x, Float y, Float& outX, Float& outY)
        {
            outX = x * m.m11 + y * m.m21 + m.m31;
            outY = x * m.m12 + y * m.m22 + m.m32;
            return std::isfinite(outX.Get()) && std::isfinite(outY.Get());
        }

        Bool Invert(const Matrix3x2& m, Matrix3x2& inverse)
        {
            const double determinant = static_cast<double>(m.m11) * m.m22 - static_cast<double>(m.m12) * m.m21;
            if (determinant == 0.0 || false == std::isfinite(determinant))
            {
                return false;
            }
            inverse.m11 = static_cast<JBro::Float>(m.m22 / determinant);
            inverse.m12 = static_cast<JBro::Float>(-m.m12 / determinant);
            inverse.m21 = static_cast<JBro::Float>(-m.m21 / determinant);
            inverse.m22 = static_cast<JBro::Float>(m.m11 / determinant);
            inverse.m31 = static_cast<JBro::Float>((static_cast<double>(m.m21) * m.m32 - static_cast<double>(m.m22) * m.m31) / determinant);
            inverse.m32 = static_cast<JBro::Float>((static_cast<double>(m.m12) * m.m31 - static_cast<double>(m.m11) * m.m32) / determinant);
            return true;
        }
    }

    void UiHookDispatcher2D::Call(GameScriptBase& script, UiHook hook)
    {
        GameScript2D& script2D = static_cast<GameScript2D&>(script);
        switch (hook)
        {
        case UiHook::PointerEnter:
            script2D.OnPointerEnter();
            break;
        case UiHook::PointerExit:
            script2D.OnPointerExit();
            break;
        case UiHook::PointerDown:
            script2D.OnPointerDown();
            break;
        case UiHook::PointerUp:
            script2D.OnPointerUp();
            break;
        case UiHook::ValueChanged:
            script2D.OnValueChanged();
            break;
        case UiHook::Submit:
            script2D.OnSubmit();
            break;
        case UiHook::DragBegin:
            script2D.OnDragBegin();
            break;
        case UiHook::DragEnd:
            script2D.OnDragEnd();
            break;
        case UiHook::Drop:
            script2D.OnDrop();
            break;
        case UiHook::ListItem:
            script2D.OnListItem();
            break;
        case UiHook::ListMeasure:
            script2D.OnListMeasure();
            break;
        case UiHook::Click:
        default:
            script2D.OnClick();
            break;
        }
    }

    void WorldLayerMapper2D::SetCanvas(Canvas* canvas)
    {
        m_canvas = canvas;
    }

    Bool WorldLayerMapper2D::PixelToLayer(const Layer& layer, const ScreenSpaceFrame& screen, Float pixelX, Float pixelY, Vector2& point) const
    {
        Float nx = 0.0f;
        Float ny = 0.0f;
        RenderCamera2D camera;
        CameraView2D view;
        // 그 픽셀이 든 뷰포트의 카메라로 그 뷰포트 안에서 역투영한다(D-359). 사각형은 그 카메라의 것으로 다시 건다.
        ScreenSpaceFrame frame = screen;
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (m_canvas == nullptr || false == Camera2DSystem::SelectCameraAt(*m_canvas, frame, pixelX, pixelY, camera))
        {
            return false;
        }
        if (false == ApplyCameraViewport(camera, frame) || false == ScreenPixelToNormalized(frame, pixelX, pixelY, nx, ny)
            || false == ComputeCameraView2D(camera, frame, view))
        {
            return false;
        }
        // 뷰 좌표 = -1..1 x 카메라의 반폭·반높이(그리기와 같은 `ComputeCameraView2D`, D-239). 월드 = 카메라 트랜스폼 x 뷰 좌표.
        Matrix3x2 cameraWorld;
        if (false == Invert(view.view, cameraWorld)
            || false == Apply(cameraWorld, nx * view.halfWidth, ny * view.halfHeight, point.x, point.y))
        {
            return false;
        }
        // **패럴랙스 레이어는 그려진 만큼 옮겨진 자리에서 누른다**(D-286). 그리기와 같은 함수로 잰 양을 빼서 오브젝트의 월드로 돌린다.
        Float offsetX = 0.0f;
        Float offsetY = 0.0f;
        if (layer.GetParallax() != 1.0f && ComputeParallaxOffset2D(view.view, layer.GetParallax(), offsetX, offsetY))
        {
            point.x -= offsetX;
            point.y -= offsetY;
        }
        return true;
    }

    Bool WorldLayerMapper2D::LayerToPixel(const Layer& layer, const ScreenSpaceFrame& screen, Vector2 point, Float& pixelX, Float& pixelY) const
    {
        RenderCamera2D camera;
        CameraView2D view;
        // 월드 → 픽셀은 주 카메라의 뷰포트다(D-359).
        ScreenSpaceFrame frame = screen;
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (m_canvas == nullptr || false == Camera2DSystem::SelectCamera(*m_canvas, camera))
        {
            return false;
        }
        if (false == ApplyCameraViewport(camera, frame) || false == ComputeCameraView2D(camera, frame, view))
        {
            return false;
        }
        Float vx = 0.0f;
        Float vy = 0.0f;
        Float offsetX = 0.0f;
        Float offsetY = 0.0f;
        if (layer.GetParallax() != 1.0f && ComputeParallaxOffset2D(view.view, layer.GetParallax(), offsetX, offsetY))
        {
            point.x += offsetX;
            point.y += offsetY;
        }
        if (false == Apply(view.view, point.x, point.y, vx, vy))
        {
            return false;
        }
        return NormalizedToScreenPixel(frame, vx / view.halfWidth, vy / view.halfHeight, pixelX, pixelY);
    }
}
