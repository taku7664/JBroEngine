#include <JBro/Framework2DSystem/Rendering/TextLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Log.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    namespace
    {
        constexpr std::uint32_t SlotMask = (1u << 28) - 1;
    }

    void TextLibrary::Initialize(AssetSystem* assets, Renderer* renderer)
    {
        Shutdown();
        m_assets = assets;
        m_renderer = renderer;
    }

    void TextLibrary::Shutdown()
    {
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (m_fonts[index])
            {
                ReleasePages(*m_fonts[index]);
            }
        }
        m_fonts.Clear();
        m_assets = nullptr;
        m_renderer = nullptr;
        m_uploadCount = 0;
    }

    void TextLibrary::ReleasePages(FontEntry& entry)
    {
        if (m_renderer != nullptr)
        {
            for (std::size_t page = 0; page < entry.pageTextures.Size(); ++page)
            {
                if (entry.pageTextures[page].generation != 0)
                {
                    m_renderer->UnregisterTexture(entry.pageTextures[page]);
                }
            }
        }
        entry.pageTextures.Clear();
    }

    bool TextLibrary::Acquire(AssetHandle font, FontView& view)
    {
        if (m_assets == nullptr || font.generation == 0)
        {
            return false;
        }
        const FontData* data = m_assets->GetFont(font);
        if (data == nullptr)
        {
            return false;
        }
        const std::uint32_t slot = font.index & SlotMask;
        if (slot >= m_fonts.Size())
        {
            // 에셋 풀은 자라도 여기서 자라는 것은 폰트를 처음 만날 때 한 번이다.
            m_fonts.Resize(static_cast<std::size_t>(slot) + 1);
        }
        if (false == static_cast<bool>(m_fonts[slot]))
        {
            m_fonts[slot] = MakeOwnerPtr<FontEntry>();
        }
        FontEntry& entry = *m_fonts[slot];

        const bool sameAsset = entry.asset.index == font.index && entry.asset.generation == font.generation;
        const bool current = sameAsset && entry.dataGeneration == data->dataGeneration && entry.face.IsLoaded();
        if (false == current)
        {
            if (sameAsset && entry.failedGeneration == data->dataGeneration && false == entry.face.IsLoaded())
            {
                return false;
            }
            // 처음이거나, 다른 에셋이 이 슬롯을 쓰게 됐거나, 같은 에셋이 재로드됐다. 옛 글리프와 페이지를 버린다.
            ReleasePages(entry);
            entry.atlas.Clear();
            entry.asset = font;
            entry.dataGeneration = data->dataGeneration;
            entry.pixelsPerUnit = data->options.pixelsPerUnit;
            entry.filter = data->options.filter;
            if (false == entry.face.Load(ArrayView<const std::byte>(data->bytes.Data(), data->bytes.Size())))
            {
                entry.failedGeneration = data->dataGeneration;
                Log::Write(LogLevel::Warning, "text", "a font asset could not be opened as TrueType or OpenType");
                return false;
            }
            entry.failedGeneration = 0;
        }
        view.face = &entry.face;
        view.atlas = &entry.atlas;
        view.pixelsPerUnit = entry.pixelsPerUnit;
        view.filter = entry.filter;
        view.dataGeneration = entry.dataGeneration;
        return true;
    }

    std::uint32_t TextLibrary::UploadDirtyPages()
    {
        if (m_renderer == nullptr)
        {
            return 0;
        }
        std::uint32_t uploaded = 0;
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (false == static_cast<bool>(m_fonts[index]))
            {
                continue;
            }
            FontEntry& entry = *m_fonts[index];
            const std::uint32_t pageCount = entry.atlas.GetPageCount();
            if (entry.pageTextures.Size() < pageCount)
            {
                entry.pageTextures.Resize(pageCount);
            }
            const std::uint32_t pageSize = entry.atlas.GetPageSize();
            for (std::uint32_t page = 0; page < pageCount; ++page)
            {
                if (false == entry.atlas.IsPageDirty(page))
                {
                    continue;
                }
                const ArrayView<const std::byte> source = entry.atlas.GetPagePixels(page);
                JArrayView<std::byte> pixels;
                pixels.data = source.Data();
                pixels.size = static_cast<std::uint32_t>(source.Size());
                AssetHandle& texture = entry.pageTextures[page];
                bool written = false;
                if (texture.generation != 0)
                {
                    written = m_renderer->UpdateTexture(texture, pixels);
                }
                else
                {
                    texture = m_renderer->RegisterTexture(Extent2D{pageSize, pageSize}, pixels);
                    written = texture.generation != 0;
                }
                if (false == written)
                {
                    // 더러움을 남겨 다음 프레임에 다시 해 본다. 그동안 그 페이지의 글자는 빈 핸들이라 그려지지 않는다.
                    continue;
                }
                entry.atlas.ClearPageDirty(page);
                ++uploaded;
            }
        }
        m_uploadCount += uploaded;
        return uploaded;
    }

    AssetHandle TextLibrary::GetPageTexture(AssetHandle font, std::uint32_t page) const
    {
        const std::uint32_t slot = font.index & SlotMask;
        if (slot >= m_fonts.Size() || false == static_cast<bool>(m_fonts[slot]))
        {
            return {};
        }
        const FontEntry& entry = *m_fonts[slot];
        if (entry.asset.index != font.index || entry.asset.generation != font.generation || page >= entry.pageTextures.Size())
        {
            return {};
        }
        return entry.pageTextures[page];
    }

    std::uint64_t TextLibrary::GetUploadCount() const
    {
        return m_uploadCount;
    }

    std::uint32_t TextLibrary::GetPageTextureCount() const
    {
        std::uint32_t count = 0;
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (false == static_cast<bool>(m_fonts[index]))
            {
                continue;
            }
            for (std::size_t page = 0; page < m_fonts[index]->pageTextures.Size(); ++page)
            {
                count += m_fonts[index]->pageTextures[page].generation != 0 ? 1u : 0u;
            }
        }
        return count;
    }
}
