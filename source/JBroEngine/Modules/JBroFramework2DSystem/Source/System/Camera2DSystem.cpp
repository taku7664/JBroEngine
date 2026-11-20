#include <JBro/Framework2DSystem/System/Camera2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

#include <cmath>
#include <limits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    namespace
    {
        Bool TryInvert(const Matrix3x2& world, Matrix3x2& view)
        {
            const double determinant = static_cast<double>(world.m11) * world.m22
                - static_cast<double>(world.m12) * world.m21;
            if (determinant == 0.0 || false == std::isfinite(determinant))
            {
                return false;
            }
            const double values[] = {
                world.m22 / determinant, -world.m12 / determinant,
                -world.m21 / determinant, world.m11 / determinant,
                (static_cast<double>(world.m32) * world.m21 - static_cast<double>(world.m31) * world.m22) / determinant,
                (static_cast<double>(world.m31) * world.m12 - static_cast<double>(world.m32) * world.m11) / determinant};
            for (double value : values)
            {
                if (false == std::isfinite(value) || std::fabs(value) > (std::numeric_limits<float>::max)())
                {
                    return false;
                }
            }
            view = {static_cast<JBro::Float>(values[0]), static_cast<JBro::Float>(values[1]),
                static_cast<JBro::Float>(values[2]), static_cast<JBro::Float>(values[3]),
                static_cast<JBro::Float>(values[4]), static_cast<JBro::Float>(values[5])};
            return true;
        }
    }

    Int32 Camera2DSystem::GetExecutionOrder() const
    {
        return 300;
    }

    void Camera2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Camera2DSystem::SetViewportTextures(ArrayView<const AssetHandle> textures)
    {
        m_viewportTextures = textures;
    }

    void Camera2DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        // **뷰포트마다 뷰 하나, 목록 차례로**(D-438, viewport-plan §4.3). 주 카메라는 화면에 그리는 첫 뷰다.
        m_renderWorld->ClearCameras();
        UInt32 unusable = 0;
        Bool hasMain = false;
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            const CanvasViewport& viewport = viewports[index];
            RenderCamera2D item;
            if (false == FillView(canvas, viewport, item, &unusable))
            {
                continue;
            }
            if (false == viewport.renderTextureId.IsNull())
            {
                // 렌더 텍스처 뷰포트는 풀린 핸들이 있어야 그린다. 핸들이 없으면(에셋을 못 찾음) 화면에 잘못 그리지 않고 뺀다.
                item.renderTexture = index < m_viewportTextures.Size() ? m_viewportTextures[static_cast<UInt32>(index)] : AssetHandle{};
                if (item.renderTexture.generation == 0)
                {
                    continue;
                }
            }
            else if (false == hasMain)
            {
                m_renderWorld->SetCamera(item);
                hasMain = true;
            }
            m_renderWorld->AddCamera(item);
        }
        m_renderWorld->SetUnusableCameraCount(unusable);
        if (unusable > m_warnedUnusable)
        {
            Log::Write(LogLevel::Warning, "camera",
                "%u Camera2D skipped - its values cannot be drawn (orthographicSize or pixelsPerUnit must be above 0, nearPlane below farPlane)",
                unusable);
        }
        m_warnedUnusable = unusable;
    }

    namespace
    {
        // 켜진 카메라이고 그 트랜스폼이 켜져 있으며 월드 행렬이 뒤집히는가. 뒤집힌 뷰를 `view` 에 둔다.
        Bool UsableCamera(Canvas& canvas, Component::Camera2D& camera, Matrix3x2& view, const Component::Transform2D*& world)
        {
            if (false == camera.IsActiveComponent())
            {
                return false;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
            world = canvas.FindComponentRaw<Component::Transform2D>(owner);
            return world != nullptr && world->IsActiveComponent() && world->worldValid && TryInvert(world->world, view);
        }
    }

    namespace
    {
        // 카메라 하나로 뷰를 채운다. 켜진 카메라가 아니면 거짓이다. 값이 그릴 수 있는지는 보지 않는다 - 부르는 쪽이 `IsDrawableCamera2D` 로 본다.
        Bool FillFromCamera(Canvas& canvas, Component::Camera2D& camera, const CanvasViewport& viewport, RenderCamera2D& item)
        {
            const Component::Transform2D* world = nullptr;
            if (false == UsableCamera(canvas, camera, item.view, world))
            {
                return false;
            }
            item.owner = Internal::CanvasAccess::GetOwner(camera);
            item.orthographicSize = camera.orthographicSize;
            item.pixelsPerUnit = camera.pixelsPerUnit;
            item.projection = camera.projection;
            item.nearPlane = camera.nearPlane;
            item.farPlane = camera.farPlane;
            item.clearColor = camera.clearColor;
            item.postProcessProfile = camera.postProcessProfile;
            item.position = world->worldPosition;
            item.viewportOrigin = viewport.origin;
            item.viewportSize = viewport.size;
            item.clearViewport = viewport.clearViewport;
            item.layers = ViewLayerMask{};
            if (false == viewport.layers.IsEmpty())
            {
                item.layers.filtered = true;
                for (std::size_t at = 0; at < viewport.layers.Size(); ++at)
                {
                    const Layer* layer = canvas.FindLayer(viewport.layers[at]);
                    if (layer != nullptr)
                    {
                        item.layers.Allow(layer->GetOrder());
                    }
                }
            }
            return true;
        }

        Bool NamesCamera(const CanvasViewport& viewport, Component::Camera2D& camera)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
            return owner != nullptr && owner->GetInstanceId() == viewport.camera;
        }
    }

    Component::Camera2D* Camera2DSystem::ResolveViewportCamera(Canvas& canvas, const CanvasViewport& viewport)
    {
        // 자동이면 그릴 수 있는 첫 켜진 카메라다(D-239 - 값이 잘못된 카메라는 건너뛴다). 정한 카메라는 그것 하나다.
        Component::Camera2D* found = nullptr;
        canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
        {
            if (found != nullptr || (viewport.HasCamera() && false == NamesCamera(viewport, camera)))
            {
                return;
            }
            RenderCamera2D item;
            if (FillFromCamera(canvas, camera, viewport, item) && (viewport.HasCamera() || IsDrawableCamera2D(item)))
            {
                found = &camera;
            }
        });
        return found;
    }

    UInt32 Camera2DSystem::CountActiveCameras(Canvas& canvas)
    {
        UInt32 count = 0;
        canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
        {
            Matrix3x2 view;
            const Component::Transform2D* world = nullptr;
            if (UsableCamera(canvas, camera, view, world))
            {
                ++count;
            }
        });
        return count;
    }

    Bool Camera2DSystem::FillView(Canvas& canvas, const CanvasViewport& viewport, RenderCamera2D& item, UInt32* unusable)
    {
        if (false == viewport.active)
        {
            return false;
        }
        // **그릴 수 없는 값의 카메라는 건너뛴다**(D-239). 자동이면 다음 켜진 카메라로 넘어가고, 정한 카메라면 그 뷰포트는 그리지 않는다.
        // 건너뛴 카메라는 센다 - 시뮬레이션 뷰가 "카메라 없음" 과 가려 까닭을 말한다.
        Bool filled = false;
        canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
        {
            if (filled || (viewport.HasCamera() && false == NamesCamera(viewport, camera)))
            {
                return;
            }
            RenderCamera2D candidate;
            if (false == FillFromCamera(canvas, camera, viewport, candidate))
            {
                return;
            }
            if (false == IsDrawableCamera2D(candidate))
            {
                if (unusable != nullptr)
                {
                    ++*unusable;
                }
                return;
            }
            item = candidate;
            filled = true;
        });
        // 빈 뷰포트는 그리지 않는다(D-359).
        return filled && HasDrawableViewport(item);
    }

    Bool Camera2DSystem::SelectCameraAt(Canvas& canvas, const ScreenSpaceFrame& frame, Float pixelX, Float pixelY, RenderCamera2D& result)
    {
        Bool found = false;
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            // 렌더 텍스처에 그리는 뷰포트는 화면에 없다 - 포인터의 카메라가 아니다(D-362).
            RenderCamera2D item;
            if (false == viewports[index].renderTextureId.IsNull() || false == FillView(canvas, viewports[index], item, nullptr))
            {
                continue;
            }
            ScreenSpaceFrame own = frame;
            ApplyCameraViewport(item, own);
            ScreenArea area;
            // 목록에서 뒤의 것이 위에 그려진다 - 겹치면 뒤의 것이 이긴다.
            if (GetScreenArea(own, area) && pixelX >= area.x && pixelX < area.x + area.width && pixelY >= area.y && pixelY < area.y + area.height)
            {
                result = item;
                found = true;
            }
        }
        return found || SelectCamera(canvas, result);
    }

    Bool Camera2DSystem::SelectCamera(Canvas& canvas, RenderCamera2D& result, UInt32* unusable)
    {
        // **화면에 그리는 첫 켜진 뷰포트**다(D-438). 그전에는 카메라의 `primary` 가, 없으면 첫 활성 카메라가 주 카메라였다(D-187) -
        // 이제 "어느 카메라" 는 뷰포트가 정하고, 새 캔버스의 기본 뷰포트가 자동(첫 켜진 카메라)이라 D-187 의 동작이 그대로 남는다.
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            if (false == viewports[index].renderTextureId.IsNull())
            {
                continue;
            }
            if (FillView(canvas, viewports[index], result, unusable))
            {
                return true;
            }
        }
        return false;
    }

    void Camera2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
