#include <JBro/TextRendering/TextLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Log.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Task/TaskManager.h>

#include <cstring>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr UInt32 SlotMask = (1u << 28) - 1;
        // 태스크 하나가 뜨는 글리프 수다. 워커끼리 나눠 갖고, 끝난 덩어리부터 아틀라스에 들어간다.
        constexpr UInt32 PrewarmChunk = 128;

        // **워커에서 글리프를 뜬다.** face 는 읽기만 한다(stb 는 전역 상태가 없다). 결과는 제 배열에 담아 두고, 아틀라스에 넣는 것은
        // `OnFinished`(메인 스레드)가 라이브러리에 맡긴다. face 의 수명은 라이브러리가 지킨다 - 다시 열거나 부수기 전에 기다린다.
        class PrewarmTask final : public Task
        {
        public:
            PrewarmTask(TextLibrary& library, UInt32 slot, const Text::FontFace& face, TextLibrary::PrewarmResult&& work)
                : Task(String("Prewarm glyphs"), static_cast<std::uint32_t>(work.glyphs.Size()))
                , m_library(library)
                , m_slot(slot)
                , m_face(face)
                , m_result(std::move(work))
            {
            }

        protected:
            void Run() override
            {
                Array<std::uint8_t> scratch;
                for (const Text::GlyphIndex glyph : m_result.glyphs)
                {
                    if (IsCancelRequested())
                    {
                        return;
                    }
                    Text::GlyphBitmapBox box;
                    Bool drawn = false;
                    if (m_result.sdfSpread != 0)
                    {
                        drawn = m_face.RasterizeGlyphSdf(glyph, static_cast<float>(m_result.pixelSize),
                            static_cast<std::int32_t>(m_result.sdfSpread), box, scratch);
                    }
                    else if (m_face.MeasureGlyphBitmap(glyph, static_cast<float>(m_result.pixelSize), box))
                    {
                        drawn = true;
                        if (box.width > 0 && box.height > 0)
                        {
                            scratch.Resize(static_cast<std::size_t>(box.width) * static_cast<std::size_t>(box.height));
                            std::memset(scratch.Data(), 0, scratch.Size());
                            m_face.RasterizeGlyph(glyph, static_cast<float>(m_result.pixelSize), box, scratch.Data(), box.width);
                        }
                    }
                    m_result.boxes.Add(drawn ? box : Text::GlyphBitmapBox{});
                    m_result.offsets.Add(static_cast<std::uint32_t>(m_result.pixels.Size()));
                    if (drawn && box.width > 0 && box.height > 0)
                    {
                        m_result.pixels.Append(scratch.Data(), static_cast<std::size_t>(box.width) * static_cast<std::size_t>(box.height));
                    }
                    SucceedSubTask();
                }
            }

            void OnFinished(const TaskResult& result) override
            {
                m_library.FinishPrewarm(m_slot, m_result, result.state == TaskState::Completed);
            }

        private:
            TextLibrary& m_library;
            UInt32 m_slot = 0;
            const Text::FontFace& m_face;
            TextLibrary::PrewarmResult m_result;
        };
    }

    void TextLibrary::Initialize(AssetSystem* assets, Renderer* renderer, TaskManager* tasks)
    {
        Shutdown();
        m_assets = assets;
        m_renderer = renderer;
        m_tasks = tasks;
    }

    void TextLibrary::Shutdown()
    {
        ReleaseProjectFonts();
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (m_fonts[index])
            {
                // 워커가 이 face 를 읽고 있을 수 있다. 끝나기를 기다린 뒤에 부순다.
                WaitForPrewarm(*m_fonts[index]);
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
        const UInt32 revision = m_assets->GetProjectFontsRevision();
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
            Bool duplicate = false;
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

    Bool TextLibrary::GetFamilyFonts(AssetHandle font, AssetHandle (&slots)[4]) const
    {
        const FontFamilyData* family = m_assets != nullptr ? m_assets->GetFontFamily(font) : nullptr;
        if (family == nullptr)
        {
            return false;
        }
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            slots[slot] = family->fonts[slot];
        }
        return true;
    }

    Bool TextLibrary::Acquire(AssetHandle font, FontView& view)
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
        const UInt32 slot = font.index & SlotMask;
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

        const Bool sameAsset = entry.asset.index == font.index && entry.asset.generation == font.generation;
        const Bool current = sameAsset && entry.dataGeneration == data->dataGeneration && entry.face.IsLoaded();
        if (false == current)
        {
            if (sameAsset && entry.failedGeneration == data->dataGeneration && false == entry.face.IsLoaded())
            {
                return false;
            }
            // 처음이거나, 다른 에셋이 이 슬롯을 쓰게 됐거나, 같은 에셋이 재로드됐다. 옛 글리프와 페이지를 버린다.
            // face 를 다시 열기 전에 옛 face 를 읽는 워커를 기다린다.
            WaitForPrewarm(entry);
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
            entry.sourceHash = data->bakedAtlas.IsEmpty() ? UInt64(0) : Text::GlyphAtlas::HashFontSource(data->bytes.Data(), data->bytes.Size());
            entry.pageLimit = 0;
            entry.lastTrimFrame = 0;
            Prewarm(entry, slot);
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

    UInt32 TextLibrary::UploadDirtyPages()
    {
        if (m_renderer == nullptr)
        {
            return 0;
        }
        UInt32 uploaded = 0;
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (false == static_cast<bool>(m_fonts[index]))
            {
                continue;
            }
            FontEntry& entry = *m_fonts[index];
            const UInt32 pageCount = entry.atlas.GetPageCount();
            if (entry.pageTextures.Size() < pageCount)
            {
                entry.pageTextures.Resize(pageCount);
            }
            const UInt32 pageSize = entry.atlas.GetPageSize();
            for (UInt32 page = 0; page < pageCount; ++page)
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
                Bool written = false;
                if (texture.generation != 0)
                {
                    // **새 칸들을 감싸는 사각형만 올린다**(text-plan §3.6). 페이지 전체(4 MB)를 올리면 새 글자가 나온 프레임마다
                    // 1.4~1.8 ms 가 들었다(D3D12·Vulkan, 벤치마크). 백엔드가 사각형을 못 올리면 전체를 올린다.
                    UInt32 x = 0;
                    UInt32 y = 0;
                    UInt32 width = 0;
                    UInt32 height = 0;
                    entry.atlas.GetPageDirtyRect(page, x, y, width, height);
                    const UInt32 rowPitch = pageSize * 4;
                    if (width > 0 && height > 0)
                    {
                        JArrayView<std::byte> region;
                        region.data = source.Data() + static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4;
                        region.size = static_cast<std::uint32_t>(source.Size() - (static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4));
                        written = m_renderer->UpdateTextureRegion(texture, x, y, width, height, region, rowPitch);
                        if (written)
                        {
                            m_uploadedBytes += static_cast<std::uint64_t>(width) * height * 4;
                        }
                    }
                    if (false == written)
                    {
                        written = m_renderer->UpdateTexture(texture, pixels);
                        m_uploadedBytes += written ? source.Size() : 0;
                    }
                }
                else
                {
                    texture = m_renderer->RegisterTexture(Extent2D{pageSize, pageSize}, pixels);
                    written = texture.generation != 0;
                    m_uploadedBytes += written ? source.Size() : 0;
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

    AssetHandle TextLibrary::GetPageTexture(AssetHandle font, UInt32 page) const
    {
        const UInt32 slot = font.index & SlotMask;
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

    Text::BakedAtlasStamp TextLibrary::PrewarmStampOf(const FontImportOptions& options, UInt64 sourceHash)
    {
        const Bool sdf = options.renderMode == FontRenderMode::Sdf;
        Text::BakedAtlasStamp stamp;
        stamp.sourceHash = sourceHash;
        stamp.set = options.prewarm == FontPrewarm::Ksx1001 ? Text::PrewarmSet::Ksx1001
            : options.prewarm == FontPrewarm::Ascii          ? Text::PrewarmSet::Ascii
                                                              : Text::PrewarmSet::None;
        stamp.pixelSize = sdf ? options.sdfSize : options.prewarmSize;
        stamp.sdfSpread = sdf ? options.sdfSpread : UInt32(0);
        return stamp;
    }

    Bool TextLibrary::BakeFontAtlas(const FontData& data, Array<std::byte>& out)
    {
        out.Clear();
        const Text::BakedAtlasStamp stamp =
            PrewarmStampOf(data.options, Text::GlyphAtlas::HashFontSource(data.bytes.Data(), data.bytes.Size()));
        if (stamp.set == Text::PrewarmSet::None)
        {
            return false;
        }
        Text::FontFace face;
        if (false == face.Load(ArrayView<const std::byte>(data.bytes.Data(), data.bytes.Size())))
        {
            return false;
        }
        Text::GlyphAtlas atlas;
        atlas.Prewarm(face, stamp.set, stamp.pixelSize, stamp.sdfSpread);
        atlas.Bake(stamp, out);
        return true;
    }

    UInt64 TextLibrary::GetBakedRestoreCount() const
    {
        return m_bakedRestores;
    }

    void TextLibrary::Prewarm(FontEntry& entry, UInt32 slot)
    {
        entry.prewarmed = 0;
        entry.prewarmPages = 0;
        if (entry.prewarm == FontPrewarm::None)
        {
            return;
        }
        // **미리 뜬 아틀라스가 있으면 되살린다**(D-232). 표지(원본 해시·벌·크기·퍼짐)가 맞을 때만이다 - 틀리면 아래에서 지금처럼 뜬다.
        // 비운 뒤(퇴출) 다시 채울 때도 같다.
        if (const FontData* data = m_assets != nullptr ? m_assets->GetFont(entry.asset) : nullptr; data != nullptr && false == data->bakedAtlas.IsEmpty())
        {
            FontImportOptions options = data->options;
            options.prewarm = entry.prewarm;
            options.prewarmSize = entry.prewarmSize;
            options.renderMode = entry.renderMode;
            options.sdfSize = entry.sdfSize;
            options.sdfSpread = entry.sdfSpread;
            if (entry.atlas.Restore(ArrayView<const std::byte>(data->bakedAtlas.Data(), data->bakedAtlas.Size()),
                    PrewarmStampOf(options, entry.sourceHash)))
            {
                entry.prewarmed = entry.atlas.GetGlyphCount();
                entry.prewarmPages = entry.atlas.GetPageCount();
                ++m_bakedRestores;
                return;
            }
            Log::Write(LogLevel::Info, "text", "a baked atlas does not match its font; prewarming at run time");
        }
        const Text::PrewarmSet set = entry.prewarm == FontPrewarm::Ksx1001 ? Text::PrewarmSet::Ksx1001 : Text::PrewarmSet::Ascii;
        const Bool sdf = entry.renderMode == FontRenderMode::Sdf;
        const UInt32 pixelSize = sdf ? entry.sdfSize : entry.prewarmSize;
        const UInt32 spread = sdf ? entry.sdfSpread : UInt32(0);
        if (m_tasks == nullptr || false == m_tasks->IsInitialized())
        {
            entry.prewarmed = entry.atlas.Prewarm(entry.face, set, pixelSize, spread);
            entry.prewarmPages = entry.atlas.GetPageCount();
            Log::Write(LogLevel::Info, "text", "a font prewarmed %u glyphs on %u atlas pages", entry.prewarmed,
                entry.atlas.GetPageCount());
            return;
        }
        // 워커에 덩어리로 나눠 맡긴다. 폰트를 여는 프레임이 글리프 수천 개의 래스터화로 멈추지 않는다.
        Array<Text::GlyphIndex> glyphs;
        Text::GlyphAtlas::CollectPrewarmGlyphs(entry.face, set, glyphs);
        OwnerPtr<TaskGroup> group = MakeOwnerPtr<TaskGroup>(String("Prewarm a font"));
        UInt32 tasks = 0;
        for (std::size_t first = 0; first < glyphs.Size(); first += PrewarmChunk)
        {
            PrewarmResult work;
            work.atlasGeneration = entry.atlasGeneration;
            work.dataGeneration = entry.dataGeneration;
            work.pixelSize = pixelSize;
            work.sdfSpread = spread;
            const std::size_t last = first + PrewarmChunk < glyphs.Size() ? first + PrewarmChunk : UInt64(glyphs.Size());
            for (std::size_t index = first; index < last; ++index)
            {
                work.glyphs.Add(glyphs[index]);
            }
            group->Add(MakeOwnerPtr<PrewarmTask>(*this, slot, entry.face, std::move(work)));
            ++tasks;
        }
        if (tasks == 0)
        {
            return;
        }
        if (m_tasks->Submit(std::move(group)) == InvalidTaskGroupId)
        {
            entry.prewarmed = entry.atlas.Prewarm(entry.face, set, pixelSize, spread);
            entry.prewarmPages = entry.atlas.GetPageCount();
            return;
        }
        entry.prewarmTasksPending += tasks;
    }

    void TextLibrary::FinishPrewarm(UInt32 slot, const PrewarmResult& result, Bool completed)
    {
        if (slot >= m_fonts.Size() || false == static_cast<bool>(m_fonts[slot]))
        {
            return;
        }
        FontEntry& entry = *m_fonts[slot];
        if (entry.prewarmTasksPending > 0)
        {
            --entry.prewarmTasksPending;
        }
        // 그사이 폰트가 다시 열렸거나 아틀라스를 비웠으면 옛 face·옛 칸의 것이다.
        if (false == completed || result.atlasGeneration != entry.atlasGeneration || result.dataGeneration != entry.dataGeneration)
        {
            return;
        }
        for (std::size_t index = 0; index < result.glyphs.Size() && index < result.boxes.Size(); ++index)
        {
            const Text::GlyphBitmapBox& box = result.boxes[index];
            const std::uint8_t* alpha = box.width > 0 && box.height > 0 ? result.pixels.Data() + result.offsets[index] : nullptr;
            if (entry.atlas.Insert(result.pixelSize, result.sdfSpread, result.glyphs[index], box, alpha))
            {
                ++entry.prewarmed;
            }
        }
        if (entry.prewarmTasksPending == 0)
        {
            entry.prewarmPages = entry.atlas.GetPageCount();
            Log::Write(LogLevel::Info, "text", "a font prewarmed %u glyphs on workers, on %u atlas pages", entry.prewarmed,
                entry.atlas.GetPageCount());
        }
    }

    void TextLibrary::WaitForPrewarm(FontEntry& entry)
    {
        // 태스크 관리자가 먼저 내려가면(호스트의 끄는 순서) 남은 콜백이 모두 불려 이 수가 이미 0 이다.
        while (entry.prewarmTasksPending > 0 && m_tasks != nullptr)
        {
            const UInt32 before = entry.prewarmTasksPending;
            m_tasks->Update();
            if (entry.prewarmTasksPending == before)
            {
                std::this_thread::yield();
            }
        }
    }

    Bool TextLibrary::IsPrewarming(AssetHandle font) const
    {
        const UInt32 slot = font.index & SlotMask;
        return slot < m_fonts.Size() && static_cast<bool>(m_fonts[slot]) && m_fonts[slot]->prewarmTasksPending > 0;
    }

    void TextLibrary::TrimAtlases(UInt64 frame)
    {
        for (std::size_t index = 0; index < m_fonts.Size(); ++index)
        {
            if (false == static_cast<bool>(m_fonts[index]))
            {
                continue;
            }
            FontEntry& entry = *m_fonts[index];
            const UInt32 limit = entry.pageLimit > m_pageLimit ? entry.pageLimit : m_pageLimit;
            // 미리 채운 페이지는 한도에 넣지 않는다. 워커가 아직 채우는 중이면 페이지가 느는 중이므로 비우지 않는다.
            if (entry.prewarmTasksPending > 0 || entry.atlas.GetPageCount() <= limit + entry.prewarmPages)
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
            Prewarm(entry, static_cast<std::uint32_t>(index));
            Log::Write(LogLevel::Info, "text", "a font atlas passed %u pages and was emptied; the text on screen draws its glyphs again",
                limit);
        }
    }

    void TextLibrary::SetPageLimit(UInt32 pages)
    {
        m_pageLimit = pages > 0 ? pages : UInt32(1);
    }

    UInt32 TextLibrary::GetTrimCount() const
    {
        return m_trimCount;
    }

    UInt32 TextLibrary::GetPrewarmedGlyphCount(AssetHandle font) const
    {
        const UInt32 slot = font.index & SlotMask;
        if (slot >= m_fonts.Size() || false == static_cast<bool>(m_fonts[slot]))
        {
            return 0;
        }
        const FontEntry& entry = *m_fonts[slot];
        return entry.asset.index == font.index && entry.asset.generation == font.generation ? entry.prewarmed : UInt32(0);
    }

    UInt64 TextLibrary::GetUploadedBytes() const
    {
        return m_uploadedBytes;
    }

    UInt64 TextLibrary::GetUploadCount() const
    {
        return m_uploadCount;
    }

    UInt32 TextLibrary::GetPageTextureCount() const
    {
        UInt32 count = 0;
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
