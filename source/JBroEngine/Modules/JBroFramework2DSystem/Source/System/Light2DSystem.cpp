#include <JBro/Framework2DSystem/System/Light2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Types/Bool.h>

#include <cmath>

namespace JBro::System
{
    Int32 Light2DSystem::GetExecutionOrder() const
    {
        return 400;
    }

    void Light2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Light2DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        canvas.ForEach<Component::Light2D>([&](Component::Light2D& light)
        {
            if (false == light.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(light);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            if (layer != nullptr && (false == layer->IsVisible() || layer->GetSpace() == LayerSpace::Screen))
            {
                return;
            }
            const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
            const Bool placed = transform != nullptr && transform->IsActiveComponent() && transform->worldValid;
            // 환경광은 자리가 없다. 다른 라이트는 자리가 있어야 한다.
            if (false == placed && light.type != Component::Light2DType::Global)
            {
                return;
            }
            Light2DRenderItem item;
            item.owner = owner;
            item.type = light.type;
            item.color = light.color;
            item.intensity = light.intensity;
            item.innerRadius = light.innerRadius;
            item.outerRadius = light.outerRadius;
            item.innerAngle = light.innerAngle;
            item.outerAngle = light.outerAngle;
            item.layerParallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            item.castShadows = light.castShadows;
            item.shadowSoftness = light.shadowSoftness;
            if (placed)
            {
                // 행 벡터 규약이라 첫 행이 오브젝트의 +x 다. 늘이거나 줄인 크기는 방향에 들지 않는다.
                item.position = Vector2{ transform->world.m31, transform->world.m32 };
                const Float length = std::sqrt(transform->world.m11 * transform->world.m11 + transform->world.m12 * transform->world.m12);
                item.direction = length > 0.00001f
                    ? Vector2{ transform->world.m11 / length, transform->world.m12 / length }
                    : Vector2{ 1.0f, 0.0f };
            }
            m_renderWorld->SubmitLight(item);
        });
    }

    void Light2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
