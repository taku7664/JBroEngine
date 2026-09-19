#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    namespace
    {
        constexpr std::uint32_t SlotMask = (1u << 28) - 1;
    }

    void SpriteLibrary::Initialize(AssetSystem* assets, Renderer* renderer)
    {
        Shutdown();
        m_assets = assets;
        m_renderer = renderer;
    }

    void SpriteLibrary::Shutdown()
    {
        if (m_renderer != nullptr)
        {
            for (std::size_t index = 0; index < m_textures.Size(); ++index)
            {
                if (m_textures[index].rendererTexture.generation != 0)
                {
                    m_renderer->UnregisterTexture(m_textures[index].rendererTexture);
                }
            }
        }
        m_textures.Clear();
        m_assets = nullptr;
        m_renderer = nullptr;
    }

    bool SpriteLibrary::EnsureTexture(AssetHandle textureAsset, AssetHandle& rendererTexture)
    {
        const TextureData* texture = m_assets->GetTexture(textureAsset);
        if (texture == nullptr)
        {
            return false;
        }
        const std::uint32_t slot = textureAsset.index & SlotMask;
        if (slot >= m_textures.Size())
        {
            // 에셋 풀은 자라도 여기서 자라는 것은 첫 만남 때 한 번이다 - 프레임마다가 아니다.
            m_textures.Resize(static_cast<std::size_t>(slot) + 1);
        }
        TextureEntry& entry = m_textures[slot];
        JArrayView<std::byte> pixels;
        pixels.data = texture->pixels.Data();
        pixels.size = static_cast<std::uint32_t>(texture->pixels.Size());
        const Extent2D extent{texture->width, texture->height};

        const bool sameAsset = entry.asset.generation == textureAsset.generation && entry.asset.index == textureAsset.index;
        if (sameAsset && entry.rendererTexture.generation != 0)
        {
            if (entry.pixelGeneration != texture->pixelGeneration)
            {
                // in-place 재로드다. 크기가 같으면 같은 핸들에 다시 올리고, 다르면 새로 등록한다.
                if (false == m_renderer->UpdateTexture(entry.rendererTexture, pixels))
                {
                    m_renderer->UnregisterTexture(entry.rendererTexture);
                    entry.rendererTexture = m_renderer->RegisterTexture(extent, pixels);
                    if (entry.rendererTexture.generation == 0)
                    {
                        return false;
                    }
                }
                entry.pixelGeneration = texture->pixelGeneration;
            }
            rendererTexture = entry.rendererTexture;
            return true;
        }

        // 다른 에셋이 이 슬롯을 쓰게 됐거나 처음이다. 옛 텍스처는 내린다.
        if (entry.rendererTexture.generation != 0)
        {
            m_renderer->UnregisterTexture(entry.rendererTexture);
            entry.rendererTexture = {};
        }
        entry.rendererTexture = m_renderer->RegisterTexture(extent, pixels);
        if (entry.rendererTexture.generation == 0)
        {
            entry.asset = {};
            return false;
        }
        entry.asset = textureAsset;
        entry.pixelGeneration = texture->pixelGeneration;
        rendererTexture = entry.rendererTexture;
        return true;
    }

    bool SpriteLibrary::Resolve(AssetHandle spriteAsset, std::uint32_t frameIndex, AssetHandle& rendererTexture, float uvRect[4],
        SpriteFrameView* frameView)
    {
        if (m_assets == nullptr || m_renderer == nullptr || uvRect == nullptr)
        {
            return false;
        }
        const SpriteData* sprite = m_assets->GetSprite(spriteAsset);
        if (sprite == nullptr || sprite->frames.IsEmpty())
        {
            return false;
        }
        const TextureData* texture = m_assets->GetTexture(sprite->texture);
        if (texture == nullptr || texture->width == 0 || texture->height == 0)
        {
            return false;
        }
        AssetHandle uploaded;
        if (false == EnsureTexture(sprite->texture, uploaded))
        {
            return false;
        }
        const std::size_t clamped = frameIndex < sprite->frames.Size() ? frameIndex : sprite->frames.Size() - 1;
        const SpriteFrame& frame = sprite->frames[clamped];
        const float width = static_cast<float>(texture->width);
        const float height = static_cast<float>(texture->height);
        rendererTexture = uploaded;
        uvRect[0] = static_cast<float>(frame.x) / width;
        uvRect[1] = static_cast<float>(frame.y) / height;
        uvRect[2] = static_cast<float>(frame.width) / width;
        uvRect[3] = static_cast<float>(frame.height) / height;
        if (frameView != nullptr)
        {
            const float pixelsPerUnit = sprite->options.pixelsPerUnit > 0.0f
                ? sprite->options.pixelsPerUnit : DefaultPixelsPerUnit;
            frameView->widthUnits = static_cast<float>(frame.width) / pixelsPerUnit;
            frameView->heightUnits = static_cast<float>(frame.height) / pixelsPerUnit;
            frameView->pivotX = frame.pivotX;
            frameView->pivotY = frame.pivotY;
            frameView->filter = texture->filter;
        }
        return true;
    }

    std::uint32_t SpriteLibrary::GetUploadedTextureCount() const
    {
        std::uint32_t count = 0;
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            count += m_textures[index].rendererTexture.generation != 0 ? 1u : 0u;
        }
        return count;
    }
}
