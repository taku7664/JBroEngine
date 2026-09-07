#include <JBro/Framework2D/System/SpriteRender2DSystem.h>

#include <JBro/Framework2D/Canvas/Canvas.h>
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
            GameObject* owner = sprite.GetOwner();
            const auto* world = canvas.GetComponent<Component::WorldTransform2D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || world->dirty)
            {
                return;
            }
            SpriteRenderItem item;
            item.owner = owner;
            item.sourceId = sprite.GetInstanceId();
            item.world = world->matrix;
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
