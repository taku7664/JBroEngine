#include <JBro/Framework2DSystem/System/SpriteRender2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

namespace JBro::System
{
    int SpriteRender2DSystem::GetExecutionOrder() const
    {
        return 400;
    }

    void SpriteRender2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void SpriteRender2DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        canvas.ForEach<Component::SpriteRenderer2D>([&](Component::SpriteRenderer2D& sprite)
        {
            if (false == sprite.visible || false == sprite.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(sprite);
            const auto* world = canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            SpriteRenderItem item;
            item.owner = owner;
            item.sourceId = sprite.GetInstanceId();
            item.world = world->world;
            item.sprite = sprite.sprite;
            item.material = sprite.material;
            item.tint = sprite.tint;
            item.pivot = sprite.pivot;
            item.size = sprite.size;
            item.renderOrder = sprite.renderOrder;
            if (sprite.flip == Component::SpriteFlip::Horizontal || sprite.flip == Component::SpriteFlip::Both)
            {
                item.size.x = -item.size.x;
            }
            if (sprite.flip == Component::SpriteFlip::Vertical || sprite.flip == Component::SpriteFlip::Both)
            {
                item.size.y = -item.size.y;
            }
            m_renderWorld->SubmitSprite(item);
        });
    }

    void SpriteRender2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
