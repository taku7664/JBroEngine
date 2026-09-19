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

    void SpriteRender2DSystem::SetSpriteLibrary(SpriteLibrary* library)
    {
        m_spriteLibrary = library;
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
            // 비가시 레이어는 렌더만 빠진다. 시뮬레이션은 계속 돈다(§7).
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            if (layer != nullptr && false == layer->IsVisible())
            {
                return;
            }
            const auto* world = canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            SpriteRenderItem item;
            item.owner = owner;
            item.sourceId = sprite.GetInstanceId();
            item.layerOrder = layer != nullptr ? layer->GetOrder() : 0;
            item.world = world->world;
            // 해석 패스가 채운 에셋 핸들을 렌더러 텍스처와 칸으로 푼다. 이 시점은 렌더러 프레임 밖(Update)이라
            // 처음 만난 텍스처의 업로드가 여기서 일어난다. 풀리지 않으면 흰색이다.
            if (m_spriteLibrary == nullptr || sprite.sprite.generation == 0
                || false == m_spriteLibrary->Resolve(sprite.sprite, sprite.frameIndex, item.texture, item.uvRect))
            {
                item.texture = {};
                item.uvRect[0] = 0.0f;
                item.uvRect[1] = 0.0f;
                item.uvRect[2] = 1.0f;
                item.uvRect[3] = 1.0f;
            }
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
