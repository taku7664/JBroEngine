#include <JBro/Framework3DSystem/System/Camera3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>

namespace JBro::System
{
    int Camera3DSystem::GetExecutionOrder() const
    {
        return 300;
    }

    void Camera3DSystem::SetRenderWorld(RenderWorld3D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Camera3DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        bool selected = false;
        canvas.ForEach<Component::Camera3D>([&](Component::Camera3D& camera)
        {
            if (selected || false == camera.primary || false == camera.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(camera);
            const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            RenderCamera3D item;
            item.owner = owner;
            item.position = world->worldPosition;
            item.rotation = world->worldRotation;
            item.projection = camera.projection;
            item.verticalFieldOfView = camera.verticalFieldOfView;
            item.orthographicSize = camera.orthographicSize;
            item.nearPlane = camera.nearPlane;
            item.farPlane = camera.farPlane;
            item.clearColor = camera.clearColor;
            m_renderWorld->SetCamera(item);
            selected = true;
        });
    }

    void Camera3DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
