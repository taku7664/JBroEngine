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
        ReleaseProjectFonts();
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

    void TextLibrary::ReleaseProjectFonts()
    {
        if (m_assets != nullptr)
        {
            for (const AssetHandle& font : m_projectFonts)
            {
                m_assets->Release(font);
            }
        }
        m_projectFonts.Clear();
        m_projectFontsSynced = false;
    }

    void TextLibrary::SyncProjectFonts()
    {
        if (m_assets == nullptr)
        {
            return;
        }
        const std::uint32_t revision = m_assets->GetProjectFontsRevision();
        if (m_projectFontsSynced && revision == m_projectFontsRevision)
        {
            return;
        }
        ReleaseProjectFonts();
        const ArrayView<const AssetId> ids = m_assets->GetProjectFonts();
        for (std::size_t index = 0; index < ids.Size(); ++index)
        {
            // 고르지 않은 줄이다. 경고할 일이 아니다.
            if (ids[index].IsNull())
            {
                continue;
            }
            const AssetHandle font = m_assets->Load(ids[index]);
            if (font.generation == 0)
            {
                Log::Write(LogLevel::Warning, "text", "a project font could not be loaded and is skipped");
                continue;
            }
            // 같은 폰트를 두 번 적었으면 한 번만 든다 - 폴백을 두 번 찾아볼 까닭이 없다.
            bool duplicate = false;
            for (const AssetHandle& held : m_projectFonts)
            {
                duplicate = duplicate || (held.index == font.index && held.generation == font.generation);
            }
            if (duplicate)
            {
                m_assets->Release(font);
                continue;
            }
            m_projectFonts.Add(font);
        }
        m_projectFontsSynced = true;
        m_projectFontsRevision = revision;
    }

    ArrayView<const AssetHandle> TextLibrary::GetProjectFonts() const
    {
        return ArrayView<const AssetHandle>(m_projectFonts.Data(), m_projectFonts.Size());
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
            entry.renderMode = data->options.renderMode;
            entry.sdfSize = data->options.sdfSize;
            entry.sdfSpread = data->options.sdfSpread;
            if (false == entry.face.Load(ArrayView<const std::byte>(data->bytes.Data(), data->bytes.Size())))
            {
                entry.failedGeneration = data->dataGeneration;
                Log::Write(LogLevel::Warning, "text", "a font asset could not be opened as TrueType or OpenType");
                return false;
            }
            entry.failedGeneration = 0;
            // 미리 뜨기(text-plan §3.6). 폰트를 열 때 한 번이다 - 그 뒤의 글이 런타임 래스터화 없이 그려진다. 올리기는 다음 업로드가 한다.
            entry.prewarm = data->options.prewarm;
            entry.prewarmSize = data->options.prewarmSize;
            entry.pageLimit = 0;
            entry.lastTrimFrame = 0;
            Prewarm(entry);
        }
        view.face = &entry.face;
        view.atlas = &entry.atlas;
        view.pixelsPerUnit = entry.pixelsPerUnit;
        view.filter = entry.filter;
        view.dataGeneration = entry.dataGeneration;
        view.renderMode = entry.renderMode;
        view.sdfSize = entry.sdfSize;
        view.sdfSpread = entry.sdfSpread;
        view.atlasGeneration = entry.atlasGeneration;
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

    void TextLibrary::Prewarm(FontEntry& entry)
    {
        entry.prewarmed = 0;
        if (entry.prewarm == FontPrewarm::None)
        {
            return;
        }
        const Text::PrewarmSet set = entry.prewarm == FontPrewarm::Ksx1001 ? Text::PrewarmSet::Ksx1001 : Text::PrewarmSet::Ascii;
        const bool sdf = entry.renderMode == FontRenderMode::Sdf;
        entry.prewarmed = entry.atlas.Prewarm(entry.face, set, sdf ? entry.sdfSize : entry.prewarmSize, sdf ? entry.sdfSpread : 0);
        Log::Write(LogLevel::Info, "text", "a font prewarmed %u glyphs on %u atlas pages", entry.prewarmed,
            entry.atlas.GetPageCount());
    }

    void TextLibrary::TrimAtlases(std::uint64_t frame)
    {
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (false == static_cast<bool>(m_fonts[index]))
            {
                continue;
            }
            FontEntry& entry = *m_fonts[index];
            const std::uint32_t limit = entry.pageLimit > m_pageLimit ? entry.pageLimit : m_pageLimit;
            if (entry.atlas.GetPageCount() <= limit)
            {
                continue;
            }
            if (entry.lastTrimFrame != 0 && frame - entry.lastTrimFrame < ThrashFrames)
            {
                // 방금 비웠는데 또 찼다. 보이는 글자만으로 한도를 넘으므로 비우면 매 프레임 다시 뜬다 - 한도를 올린다.
                entry.pageLimit = limit * 2;
                Log::Write(LogLevel::Warning, "text", "a font needs more than %u atlas pages for the text on screen; the limit is now %u",
                    limit, entry.pageLimit);
                continue;
            }
            ReleasePages(entry);
            entry.atlas.Clear();
            ++entry.atlasGeneration;
            entry.lastTrimFrame = frame;
            ++m_trimCount;
            Prewarm(entry);
            Log::Write(LogLevel::Info, "text", "a font atlas passed %u pages and was emptied; the text on screen draws its glyphs again",
                limit);
        }
    }

    void TextLibrary::SetPageLimit(std::uint32_t pages)
    {
        m_pageLimit = pages > 0 ? pages : 1;
    }

    std::uint32_t TextLibrary::GetTrimCount() const
    {
        return m_trimCount;
    }

    std::uint32_t TextLibrary::GetPrewarmedGlyphCount(AssetHandle font) const
    {
        const std::uint32_t slot = font.index & SlotMask;
        if (slot >= m_fonts.Size() || false == static_cast<bool>(m_fonts[slot]))
        {
            return 0;
        }
        const FontEntry& entry = *m_fonts[slot];
        return entry.asset.index == font.index && entry.asset.generation == font.generation ? entry.prewarmed : 0;
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
