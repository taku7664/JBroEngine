#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // 핸들 번호에서 슬롯만 떼는 마스크다. 에셋 시스템의 것을 쓴다 - 따로 적어 둔 28 비트는 타입이 위 8 비트로 옮겨진 뒤(D-322 (2)) 타입을 슬롯에 섞었다(`TextLibrary` 와 같은 고장).
        constexpr UInt32 SlotMask = (1u << 24) - 1; // `AssetSystem::SlotMask` 와 같아야 한다(그쪽은 private)
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

    Bool SpriteLibrary::EnsureTexture(AssetHandle textureAsset, AssetHandle& rendererTexture)
    {
        const TextureData* texture = m_assets->GetTexture(textureAsset);
        if (texture == nullptr)
        {
            return false;
        }
        const UInt32 slot = textureAsset.index & SlotMask;
        if (slot >= m_textures.Size())
        {
            // 에셋 풀은 자라도 여기서 자라는 것은 첫 만남 때 한 번이다 - 프레임마다가 아니다.
            m_textures.Resize(static_cast<std::size_t>(slot) + 1);
        }
        TextureEntry& entry = m_textures[slot];
        JArrayView<std::byte> pixels;
        pixels.data = texture->pixels.Data();
        pixels.size = static_cast<JBro::UInt32>(texture->pixels.Size());
        const Extent2D extent{texture->width, texture->height};

        const Bool sameAsset = entry.asset.generation == textureAsset.generation && entry.asset.index == textureAsset.index;
        if (sameAsset && entry.rendererTexture.generation == 0 && entry.failedGeneration == texture->pixelGeneration)
        {
            return false;
        }
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
                        entry.failedGeneration = texture->pixelGeneration;
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
            entry.asset = textureAsset;
            entry.failedGeneration = texture->pixelGeneration;
            return false;
        }
        entry.asset = textureAsset;
        entry.pixelGeneration = texture->pixelGeneration;
        rendererTexture = entry.rendererTexture;
        return true;
    }

    Bool SpriteLibrary::Resolve(AssetHandle spriteAsset, UInt32 frameIndex, AssetHandle& rendererTexture, Float uvRect[4],
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
        const std::size_t clamped = frameIndex < sprite->frames.Size() ? static_cast<std::size_t>(frameIndex.Get()) : sprite->frames.Size() - 1;
        const SpriteFrame& frame = sprite->frames[clamped];
        const Float width = static_cast<JBro::Float>(texture->width);
        const Float height = static_cast<JBro::Float>(texture->height);
        rendererTexture = uploaded;
        uvRect[0] = static_cast<JBro::Float>(frame.x) / width;
        uvRect[1] = static_cast<JBro::Float>(frame.y) / height;
        uvRect[2] = static_cast<JBro::Float>(frame.width) / width;
        uvRect[3] = static_cast<JBro::Float>(frame.height) / height;
        if (frameView != nullptr)
        {
            // 로드가 이미 바로잡은 값이다. 그래도 0 나누기는 여기서 한 번 더 막는다.
            const Float pixelsPerUnit = sprite->options.pixelsPerUnit > 0.0f
                ? sprite->options.pixelsPerUnit : DefaultPixelsPerUnit;
            frameView->widthUnits = static_cast<JBro::Float>(frame.width) / pixelsPerUnit;
            frameView->heightUnits = static_cast<JBro::Float>(frame.height) / pixelsPerUnit;
            frameView->widthPixels = static_cast<JBro::Float>(frame.width);
            frameView->heightPixels = static_cast<JBro::Float>(frame.height);
            frameView->pivotX = frame.pivotX;
            frameView->pivotY = frame.pivotY;
            frameView->filter = texture->filter;
        }
        return true;
    }

    UInt32 SpriteLibrary::GetUploadedTextureCount() const
    {
        UInt32 count = 0;
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            count += m_textures[index].rendererTexture.generation != 0 ? 1u : 0u;
        }
        return count;
    }
}
