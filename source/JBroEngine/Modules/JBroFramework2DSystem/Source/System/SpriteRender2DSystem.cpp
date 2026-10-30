#include <JBro/Framework2DSystem/System/SpriteRender2DSystem.h>

#include <JBro/Framework2DSystem/Rendering/ClipRect2D.h>
#include <JBro/Framework2DSystem/Rendering/GroupState2D.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    Int32 SpriteRender2DSystem::GetExecutionOrder() const
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

    void SpriteRender2DSystem::SetMaterialLibrary(MaterialLibrary* library)
    {
        m_materialLibrary = library;
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
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(sprite);
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
            item.screenSpace = layer != nullptr && layer->GetSpace() == LayerSpace::Screen;
            item.scaleMode = layer != nullptr ? layer->GetScaleMode() : ScreenScaleMode::FixedHeight;
            item.layerBlend = layer != nullptr ? layer->GetBlend() : LayerBlend::Normal;
            item.layerOpacity = layer != nullptr ? layer->GetOpacity() : Float(1.0f);
            item.layerParallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            item.layerLit = layer == nullptr || layer->IsLit();
            item.clipped = ComputeClipRect2D(canvas, owner, item.clip);
            // 해석 패스가 채운 에셋 핸들을 렌더러 텍스처와 칸으로 푼다. 이 시점은 렌더러 프레임 밖(Update)이라
            // 처음 만난 텍스처의 업로드가 여기서 일어난다. 풀리지 않으면 흰색이다.
            SpriteFrameView frame;
            Bool resolved = false;
            // 렌더 텍스처(D-362)가 먼저다 - 카메라가 그린 텍스처 전체를 보인다. 크기는 픽셀 / 100 유닛(화면 레이어는 픽셀), 피벗은 가운데다.
            if (m_spriteLibrary != nullptr && sprite.renderTexture.generation != 0)
            {
                TextureHandle target;
                Extent2D extent;
                TextureFilter filter = TextureFilter::Linear;
                if (m_spriteLibrary->ResolveRenderTexture(sprite.renderTexture, item.texture, target, extent, filter))
                {
                    constexpr Float RenderTexturePixelsPerUnit = 100.0f;
                    item.uvRect[0] = 0.0f;
                    item.uvRect[1] = 0.0f;
                    item.uvRect[2] = 1.0f;
                    item.uvRect[3] = 1.0f;
                    frame.widthPixels = static_cast<JBro::Float>(extent.width);
                    frame.heightPixels = static_cast<JBro::Float>(extent.height);
                    frame.widthUnits = frame.widthPixels / RenderTexturePixelsPerUnit;
                    frame.heightUnits = frame.heightPixels / RenderTexturePixelsPerUnit;
                    frame.filter = filter;
                    resolved = true;
                }
            }
            if (false == resolved)
            {
                resolved = m_spriteLibrary != nullptr && sprite.sprite.generation != 0
                    && m_spriteLibrary->Resolve(sprite.sprite, sprite.frameIndex, item.texture, item.uvRect, &frame);
            }
            if (false == resolved)
            {
                item.texture = {};
                item.uvRect[0] = 0.0f;
                item.uvRect[1] = 0.0f;
                item.uvRect[2] = 1.0f;
                item.uvRect[3] = 1.0f;
            }
            item.filter = resolved ? frame.filter : TextureFilter::Nearest;
            // 해석 패스가 채운 머티리얼 에셋을 렌더러 머티리얼로 푼다(D-347). 처음 보는 머티리얼은 여기서 셰이더를 굽고 올린다.
            item.material = m_materialLibrary != nullptr ? m_materialLibrary->Resolve(sprite.material) : AssetHandle{};
            item.tint = sprite.tint;
            // 그룹의 알파를 곱한다(D-358). 틴트 값은 손대지 않는다.
            item.tint.A = item.tint.A * ComputeGroupAlpha2D(canvas, owner);
            // 크기와 피벗은 에셋이 정한다(D-117). 풀리지 않은 스프라이트와 `Custom` 만 저작 값이다. 둘은 따로 고른다.
            // 화면 레이어의 좌표는 기준 픽셀이라 에셋 크기도 픽셀이다(D-237).
            item.size = (resolved && sprite.sizeMode == Component::SpriteSizeMode::FromSprite)
                ? (item.screenSpace ? Vector2{ frame.widthPixels, frame.heightPixels } : Vector2{ frame.widthUnits, frame.heightUnits })
                : sprite.size;
            item.pivot = (resolved && sprite.pivotMode == Component::SpritePivotMode::FromSprite)
                ? Vector2{ frame.pivotX, frame.pivotY } : sprite.pivot;
            item.renderOrder = sprite.renderOrder;
            item.drawSequence = sprite.drawSequence;
            // 펴 그리기(D-351). 테두리와 칸 크기는 `size` 와 같은 단위로 넘긴다. 풀리지 않은 스프라이트는 0 이라 `Simple` 로 그려진다.
            item.drawMode = sprite.drawMode;
            item.fillAmount = sprite.fillAmount;
            item.fillOrigin = sprite.fillOrigin;
            if (resolved)
            {
                const Float unitsPerPixel = item.screenSpace ? Float(1.0f)
                    : (frame.widthPixels > 0.0f ? frame.widthUnits / frame.widthPixels : Float(0.0f));
                item.tileWidth = item.screenSpace ? frame.widthPixels : frame.widthUnits;
                item.tileHeight = item.screenSpace ? frame.heightPixels : frame.heightUnits;
                item.border[0] = frame.borderLeft * unitsPerPixel;
                item.border[1] = frame.borderTop * unitsPerPixel;
                item.border[2] = frame.borderRight * unitsPerPixel;
                item.border[3] = frame.borderBottom * unitsPerPixel;
            }
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

    void SpriteRender2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
