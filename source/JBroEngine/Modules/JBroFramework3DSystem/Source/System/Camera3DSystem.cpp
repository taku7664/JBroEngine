#include <JBro/Framework3DSystem/System/Camera3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

#include <cmath>

namespace JBro::System
{
    Int32 Camera3DSystem::GetExecutionOrder() const
    {
        return 300;
    }

    void Camera3DSystem::SetRenderWorld(RenderWorld3D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    namespace
    {
        const Component::Transform3D* UsableCamera(Canvas& canvas, Component::Camera3D& camera)
        {
            if (false == camera.IsActiveComponent())
            {
                return nullptr;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
            const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return nullptr;
            }
            return world;
        }
    }

    Component::Camera3D* Camera3DSystem::ResolveViewportCamera(Canvas& canvas, const CanvasViewport& viewport)
    {
        Component::Camera3D* found = nullptr;
        canvas.ForEach<Component::Camera3D>([&](Component::Camera3D& camera)
        {
            if (found != nullptr)
            {
                return;
            }
            if (viewport.HasCamera())
            {
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
                if (owner == nullptr || owner->GetInstanceId() != viewport.camera)
                {
                    return;
                }
            }
            if (UsableCamera(canvas, camera) != nullptr)
            {
                found = &camera;
            }
        });
        return found;
    }

    UInt32 Camera3DSystem::CountActiveCameras(Canvas& canvas)
    {
        UInt32 count = 0;
        canvas.ForEach<Component::Camera3D>([&](Component::Camera3D& camera)
        {
            if (UsableCamera(canvas, camera) != nullptr)
            {
                ++count;
            }
        });
        return count;
    }

    Bool Camera3DSystem::FillView(Canvas& canvas, const CanvasViewport& viewport, RenderCamera3D& item)
    {
        if (false == viewport.active)
        {
            return false;
        }
        Component::Camera3D* camera = ResolveViewportCamera(canvas, viewport);
        if (camera == nullptr)
        {
            return false;
        }
        Object::GameObject* owner = Internal::CanvasAccess::GetOwner(*camera);
        const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
        if (world == nullptr)
        {
            return false;
        }
        item.owner = owner;
        item.position = world->worldPosition;
        item.rotation = world->worldRotation;
        item.projection = camera->projection;
        item.verticalFieldOfView = camera->verticalFieldOfView;
        item.orthographicSize = camera->orthographicSize;
        item.nearPlane = camera->nearPlane;
        item.farPlane = camera->farPlane;
        item.clearColor = camera->clearColor;
        item.background = camera->background;
        item.postProcessProfile = camera->postProcessProfile;
        item.viewportOrigin = viewport.origin;
        item.viewportSize = viewport.size;
        item.viewportOriginPixels = viewport.originPixels;
        item.viewportSizePixels = viewport.sizePixels;
        item.clearViewport = viewport.clearViewport;
        // 월드 레이어와 화면 레이어를 따로 읽는다(D-439).
        BuildViewportLayerMasks(canvas, viewport, item.layers, item.screenLayers);
        // 어떤 대상에서도 빈 사각형이면 그리지 않는다(2D 의 `HasDrawableViewport` 와 같다). 대상에서 실제로 비는지는 브리지가 잰다.
        const auto positive = [](Float ratio, Float pixels) {
            return (std::isfinite(ratio.Get()) && ratio > 0.0f) || (std::isfinite(pixels.Get()) && pixels > 0.0f);
        };
        return positive(viewport.size.x, viewport.sizePixels.x) && positive(viewport.size.y, viewport.sizePixels.y);
    }

    Bool Camera3DSystem::SelectCamera(Canvas& canvas, RenderCamera3D& result)
    {
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            if (false == viewports[index].DrawsToTexture() && FillView(canvas, viewports[index], result))
            {
                return true;
            }
        }
        return false;
    }

    void Camera3DSystem::CollectScreenLayerFrames(Canvas& canvas, const ScreenSpaceFrame& base, Array<ScreenLayerFrame>& frames)
    {
        frames.Clear();
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            const CanvasViewport& viewport = viewports[index];
            if (viewport.DrawsToTexture() || viewport.layers.IsEmpty())
            {
                continue;
            }
            RenderCamera3D item;
            if (false == FillView(canvas, viewport, item) || false == item.screenLayers.Any())
            {
                continue;
            }
            ScreenLayerFrame entry;
            entry.frame = base;
            Float x = 0.0f;
            Float y = 0.0f;
            Float width = 0.0f;
            Float height = 0.0f;
            if (false == ResolveViewportPixels(viewport, base.targetWidth, base.targetHeight, x, y, width, height))
            {
                continue;
            }
            // 대상 전체면 사각형을 비운다(대상 전체의 뜻) - 2D 의 `ApplyCameraViewport` 와 같다.
            if (false == (x <= 0.0f && y <= 0.0f && width >= base.targetWidth && height >= base.targetHeight))
            {
                entry.frame.areaX = x;
                entry.frame.areaY = y;
                entry.frame.areaWidth = width;
                entry.frame.areaHeight = height;
            }
            for (std::size_t at = 0; at < canvas.GetLayerCount(); ++at)
            {
                const Layer* layer = canvas.GetLayerAt(at);
                if (layer != nullptr && layer->GetSpace() == LayerSpace::Screen && item.screenLayers.Draws(layer->GetOrder()))
                {
                    entry.layerOrder = static_cast<std::uint16_t>(layer->GetOrder());
                    frames.Add(entry);
                }
            }
        }
    }

    void Camera3DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        // **뷰포트마다 뷰 하나, 목록 차례로**(D-438). 전에는 `primary` 를 켠 카메라 하나만 그렸고, 없으면 아무것도 그리지 않았다.
        m_renderWorld->ClearCameras();
        Bool hasMain = false;
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            const CanvasViewport& viewport = viewports[index];
            RenderCamera3D item;
            if (false == FillView(canvas, viewport, item))
            {
                continue;
            }
            // **렌더 텍스처 뷰포트**(D-439). 풀린 핸들이 있어야 그린다 - 없으면(에셋을 못 찾음) 화면에 잘못 그리지 않고 뺀다. 주 카메라가 아니다.
            if (viewport.DrawsToTexture())
            {
                item.renderTexture = viewport.renderTexture;
                if (item.renderTexture.generation != 0)
                {
                    m_renderWorld->AddCamera(item);
                }
                continue;
            }
            if (false == hasMain)
            {
                m_renderWorld->SetCamera(item);
                hasMain = true;
            }
            m_renderWorld->AddCamera(item);
        }
    }

    void Camera3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
