#include <JBro/Framework3DSystem/System/Light3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Light3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Math3D.h>

namespace JBro::System
{
    Int32 Light3DSystem::GetExecutionOrder() const
    {
        return 400;
    }

    void Light3DSystem::SetRenderWorld(RenderWorld3D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Light3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        if (m_renderWorld == nullptr)
        {
            return;
        }
        canvas.ForEach<Component::Light3D>([&](Component::Light3D& light)
        {
            if (false == light.IsActiveComponent())
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(light);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            if (layer != nullptr && false == layer->IsVisible())
            {
                return;
            }
            const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
            const Bool placed = world != nullptr && world->IsActiveComponent() && world->worldValid;
            if (false == placed && light.type != Component::Light3DType::Ambient)
            {
                return;
            }
            LightRenderItem3D item;
            item.owner = owner;
            item.type = light.type;
            item.color = light.color;
            item.intensity = light.intensity;
            item.range = light.range;
            if (placed)
            {
                item.position = world->worldPosition;
                item.forward = Rotate(world->worldRotation, Vector3{0.0f, 0.0f, -1.0f});
            }
            m_renderWorld->SubmitLight(item);
        });
    }
}
