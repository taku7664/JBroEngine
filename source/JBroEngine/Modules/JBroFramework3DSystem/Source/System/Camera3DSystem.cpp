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
        item.postProcessProfile = camera->postProcessProfile;
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
        // 빈 사각형은 그리지 않는다(2D 의 `HasDrawableViewport` 와 같다).
        return item.viewportSize.x > 0.0f && item.viewportSize.y > 0.0f && item.viewportOrigin.x < 1.0f && item.viewportOrigin.y < 1.0f;
    }

    Bool Camera3DSystem::SelectCamera(Canvas& canvas, RenderCamera3D& result)
    {
        const Array<CanvasViewport>& viewports = canvas.GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            if (viewports[index].renderTextureId.IsNull() && FillView(canvas, viewports[index], result))
            {
                return true;
            }
        }
        return false;
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
            if (false == viewport.renderTextureId.IsNull())
            {
                // 3D 렌더 텍스처는 그것을 보일 3D 쪽 자리(머티리얼의 텍스처 칸)가 없어 아직 그리지 않는다(viewport-plan §6).
                if (false == m_warnedRenderTexture)
                {
                    m_warnedRenderTexture = true;
                    Log::Write(LogLevel::Warning, "camera", "a 3D viewport draws into a render texture - 3D render textures are not drawn yet, the viewport is skipped");
                }
                continue;
            }
            RenderCamera3D item;
            if (false == FillView(canvas, viewport, item))
            {
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
