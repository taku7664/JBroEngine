#include <JBro/Framework3DSystem/System/Light3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Light3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Bool.h>
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

    void Light3DSystem::ExtractRenderWorld(Canvas& canvas)
    {
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
            const auto* transform = canvas.FindComponentRaw<Component::Transform3D>(owner);
            const Bool placed = transform != nullptr && transform->IsActiveComponent() && transform->worldValid;
            // 환경광은 자리가 없다. 다른 라이트는 자리와 방향이 있어야 한다.
            if (false == placed && light.type != Component::Light3DType::Ambient)
            {
                return;
            }
            Light3DRenderItem item;
            item.owner = owner;
            item.type = light.type;
            item.color = light.color;
            item.intensity = light.intensity;
            item.range = light.range;
            item.innerAngle = light.innerAngle;
            item.outerAngle = light.outerAngle;
            if (placed)
            {
                // 빛은 오브젝트의 로컬 -Z 로 간다(카메라·리스너와 같은 규약). 크기는 방향에 들지 않는다.
                item.position = transform->worldPosition;
                item.direction = Normalize(Rotate(transform->worldRotation, Vector3{0.0f, 0.0f, -1.0f}));
            }
            m_renderWorld->SubmitLight(item);
        });
    }

    void Light3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
