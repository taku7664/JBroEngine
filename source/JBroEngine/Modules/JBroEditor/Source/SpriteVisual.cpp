#include <JBro/Editor/SpriteVisual.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>

namespace JBro
{
    SpriteVisual ResolveSpriteVisual(const AssetSystem* assets, const Component::SpriteRenderer2D& sprite, const Layer* layer)
    {
        SpriteVisual visual;
        visual.size = sprite.size;
        visual.pivot = sprite.pivot;
        const SpriteData* data = assets != nullptr ? assets->GetSprite(sprite.sprite) : nullptr;
        if (data != nullptr && false == data->frames.IsEmpty() && data->options.pixelsPerUnit > 0.0f)
        {
            // 칸 번호가 넘치면 마지막 칸이다. 그리는 쪽(`SpriteLibrary::Resolve`)과 같다.
            const std::size_t frameIndex = sprite.frameIndex < data->frames.Size()
                ? static_cast<std::size_t>(sprite.frameIndex.Get())
                : data->frames.Size() - 1;
            const SpriteFrame& frame = data->frames[frameIndex];
            if (sprite.sizeMode == Component::SpriteSizeMode::FromSprite)
            {
                // 화면 레이어는 PPU 대신 픽셀이다 - 그리는 쪽과 같다(D-237).
                const Float ppu = layer != nullptr && layer->GetSpace() == LayerSpace::Screen ? Float(1.0f) : data->options.pixelsPerUnit;
                visual.size = { static_cast<JBro::Float>(frame.width) / ppu, static_cast<JBro::Float>(frame.height) / ppu };
            }
            if (sprite.pivotMode == Component::SpritePivotMode::FromSprite)
            {
                visual.pivot = { frame.pivotX, frame.pivotY };
            }
        }
        if (sprite.flip == Component::SpriteFlip::Horizontal || sprite.flip == Component::SpriteFlip::Both)
        {
            visual.size.x = -visual.size.x;
        }
        if (sprite.flip == Component::SpriteFlip::Vertical || sprite.flip == Component::SpriteFlip::Both)
        {
            visual.size.y = -visual.size.y;
        }
        return visual;
    }
}
