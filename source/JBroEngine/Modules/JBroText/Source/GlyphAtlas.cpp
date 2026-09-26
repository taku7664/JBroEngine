#include <JBro/Text/GlyphAtlas.h>

#include "Ksx1001Hangul.generated.h"

#include <algorithm>
#include <cstring>

namespace JBro::Text
{
    namespace
    {
        // 칸 사이와 페이지 가장자리에 비워 두는 픽셀이다. Linear 샘플링이 이웃 칸을 번지게 읽지 않게 한다.
        constexpr std::uint32_t Gap = 1;
    }

    GlyphAtlas::GlyphAtlas(std::uint32_t pageSize)
        : m_pageSize(std::clamp<std::uint32_t>(pageSize, 16, 4096))
    {
    }

    std::uint64_t GlyphAtlas::Key(std::uint32_t pixelSize, GlyphIndex glyph)
    {
        return (static_cast<std::uint64_t>(pixelSize) << 32) | static_cast<std::uint64_t>(glyph);
    }

    std::uint64_t GlyphAtlas::SdfKey(std::uint32_t pixelSize, std::uint32_t spread, GlyphIndex glyph)
    {
        // 맨 위 비트가 SDF 표시이고, 그 아래 8 비트가 퍼짐이다. 비트맵 키는 크기가 512 이하라 이 자리에 닿지 않는다.
        return (std::uint64_t{ 1 } << 63) | (static_cast<std::uint64_t>(spread & 0xFF) << 48)
            | (static_cast<std::uint64_t>(pixelSize) << 32) | static_cast<std::uint64_t>(glyph);
    }

    AtlasError GlyphAtlas::EnsureSdf(const FontFace& face, std::uint32_t pixelSize, std::uint32_t spread, GlyphIndex glyph,
        AtlasGlyph& out)
    {
        if (false == face.IsLoaded())
        {
            return AtlasError::FaceNotLoaded;
        }
        if (pixelSize == 0 || pixelSize > MaxPixelSize || spread == 0 || spread > MaxSdfSpread)
        {
            return AtlasError::InvalidPixelSize;
        }
        const std::uint64_t key = SdfKey(pixelSize, spread, glyph);
        if (const AtlasGlyph* found = m_glyphs.Find(key))
        {
            out = *found;
            return AtlasError::None;
        }
        GlyphBitmapBox box;
        if (false == face.RasterizeGlyphSdf(glyph, static_cast<float>(pixelSize), static_cast<std::int32_t>(spread), box, m_scratch)
            || box.width <= 0 || box.height <= 0)
        {
            AtlasGlyph entry;
            m_glyphs.FindOrAdd(key) = entry;
            out = entry;
            return AtlasError::None;
        }
        return Place(key, box, m_scratch.Data(), out);
    }

    AtlasError GlyphAtlas::Place(std::uint64_t key, const GlyphBitmapBox& box, const std::uint8_t* alpha, AtlasGlyph& out)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(box.width);
        const std::uint32_t height = static_cast<std::uint32_t>(box.height);
        std::uint32_t page = 0;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        if (false == Allocate(width, height, page, x, y))
        {
            return AtlasError::GlyphTooLarge;
        }
        Page& target = m_pages[page];
        const std::size_t rowBytes = static_cast<std::size_t>(m_pageSize) * 4;
        for (std::uint32_t row = 0; row < height; ++row)
        {
            std::byte* destination = target.pixels.Data() + (static_cast<std::size_t>(y) + row) * rowBytes + static_cast<std::size_t>(x) * 4;
            const std::uint8_t* source = alpha + static_cast<std::size_t>(row) * width;
            for (std::uint32_t column = 0; column < width; ++column)
            {
                destination[column * 4 + 0] = std::byte{ 255 };
                destination[column * 4 + 1] = std::byte{ 255 };
                destination[column * 4 + 2] = std::byte{ 255 };
                destination[column * 4 + 3] = static_cast<std::byte>(source[column]);
            }
        }
        if (false == target.dirty)
        {
            target.dirtyMinX = x;
            target.dirtyMinY = y;
            target.dirtyMaxX = x + width;
            target.dirtyMaxY = y + height;
        }
        else
        {
            target.dirtyMinX = std::min(target.dirtyMinX, x);
            target.dirtyMinY = std::min(target.dirtyMinY, y);
            target.dirtyMaxX = std::max(target.dirtyMaxX, x + width);
            target.dirtyMaxY = std::max(target.dirtyMaxY, y + height);
        }
        target.dirty = true;

        AtlasGlyph entry;
        entry.page = static_cast<std::uint16_t>(page);
        entry.x = static_cast<std::uint16_t>(x);
        entry.y = static_cast<std::uint16_t>(y);
        entry.width = static_cast<std::uint16_t>(width);
        entry.height = static_cast<std::uint16_t>(height);
        entry.left = static_cast<std::int16_t>(box.left);
        entry.top = static_cast<std::int16_t>(box.top);
        entry.empty = false;
        m_glyphs.FindOrAdd(key) = entry;
        out = entry;
        return AtlasError::None;
    }

    std::uint32_t GlyphAtlas::Prewarm(const FontFace& face, PrewarmSet set, std::uint32_t pixelSize, std::uint32_t sdfSpread)
    {
        if (set == PrewarmSet::None || false == face.IsLoaded())
        {
            return 0;
        }
        const std::uint32_t before = GetGlyphCount();
        const auto warm = [&](char32_t codepoint) {
            const GlyphIndex glyph = face.FindGlyph(codepoint);
            if (glyph == MissingGlyph)
            {
                return;
            }
            AtlasGlyph cell;
            if (sdfSpread == 0)
            {
                Ensure(face, pixelSize, glyph, cell);
            }
            else
            {
                EnsureSdf(face, pixelSize, sdfSpread, glyph, cell);
            }
        };
        for (char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint)
        {
            warm(codepoint);
        }
        if (set == PrewarmSet::Ksx1001)
        {
            for (const std::uint16_t syllable : Detail::Ksx1001Hangul)
            {
                warm(static_cast<char32_t>(syllable));
            }
        }
        return GetGlyphCount() - before;
    }

    bool GlyphAtlas::Insert(std::uint32_t pixelSize, std::uint32_t sdfSpread, GlyphIndex glyph, const GlyphBitmapBox& box,
        const std::uint8_t* alpha)
    {
        const std::uint64_t key = sdfSpread == 0 ? Key(pixelSize, glyph) : SdfKey(pixelSize, sdfSpread, glyph);
        if (m_glyphs.Find(key) != nullptr)
        {
            return false;
        }
        AtlasGlyph cell;
        if (box.width <= 0 || box.height <= 0 || alpha == nullptr)
        {
            m_glyphs.FindOrAdd(key) = cell;
            return true;
        }
        return Place(key, box, alpha, cell) == AtlasError::None;
    }

    void GlyphAtlas::CollectPrewarmGlyphs(const FontFace& face, PrewarmSet set, Array<GlyphIndex>& glyphs)
    {
        glyphs.Clear();
        if (set == PrewarmSet::None || false == face.IsLoaded())
        {
            return;
        }
        const auto collect = [&](char32_t codepoint) {
            const GlyphIndex glyph = face.FindGlyph(codepoint);
            if (glyph != MissingGlyph)
            {
                glyphs.Add(glyph);
            }
        };
        for (char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint)
        {
            collect(codepoint);
        }
        if (set == PrewarmSet::Ksx1001)
        {
            for (const std::uint16_t syllable : Detail::Ksx1001Hangul)
            {
                collect(static_cast<char32_t>(syllable));
            }
        }
    }

    const AtlasGlyph* GlyphAtlas::Find(std::uint32_t pixelSize, GlyphIndex glyph) const
    {
        return m_glyphs.Find(Key(pixelSize, glyph));
    }

    AtlasError GlyphAtlas::Ensure(const FontFace& face, std::uint32_t pixelSize, GlyphIndex glyph, AtlasGlyph& out)
    {
        if (false == face.IsLoaded())
        {
            return AtlasError::FaceNotLoaded;
        }
        if (pixelSize == 0 || pixelSize > MaxPixelSize)
        {
            return AtlasError::InvalidPixelSize;
        }
        const std::uint64_t key = Key(pixelSize, glyph);
        if (const AtlasGlyph* found = m_glyphs.Find(key))
        {
            out = *found;
            return AtlasError::None;
        }

        GlyphBitmapBox box;
        AtlasGlyph entry;
        if (false == face.MeasureGlyphBitmap(glyph, static_cast<float>(pixelSize), box) || box.width <= 0 || box.height <= 0)
        {
            // 그릴 것이 없는 글리프(공백 등)도 기억해 두어 다음에 다시 재지 않는다.
            m_glyphs.FindOrAdd(key) = entry;
            out = entry;
            return AtlasError::None;
        }
        // 칸이 들어갈 자리가 없으면 래스터화하지 않는다(Place 가 거절한다) - 크기만 먼저 본다.
        if (static_cast<std::uint32_t>(box.width) + 2 * Gap > m_pageSize || static_cast<std::uint32_t>(box.height) + 2 * Gap > m_pageSize)
        {
            return AtlasError::GlyphTooLarge;
        }
        const std::uint32_t width = static_cast<std::uint32_t>(box.width);
        m_scratch.Resize(static_cast<std::size_t>(width) * static_cast<std::uint32_t>(box.height));
        std::memset(m_scratch.Data(), 0, m_scratch.Size());
        face.RasterizeGlyph(glyph, static_cast<float>(pixelSize), box, m_scratch.Data(), static_cast<std::int32_t>(width));
        return Place(key, box, m_scratch.Data(), out);
    }

    bool GlyphAtlas::Allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& page, std::uint32_t& x, std::uint32_t& y)
    {
        if (width + 2 * Gap > m_pageSize || height + 2 * Gap > m_pageSize)
        {
            return false;
        }
        // 마지막 페이지에만 넣는다. 앞 페이지의 남은 선반을 되짚지 않는다 - 칸이 옮겨 다니지 않고, 찾는 비용이 페이지 수와 무관하다.
        if (false == m_pages.IsEmpty())
        {
            Page& last = m_pages[m_pages.Size() - 1];
            if (last.cursorX + width + Gap > m_pageSize)
            {
                last.cursorX = Gap;
                last.cursorY += last.shelfHeight + Gap;
                last.shelfHeight = 0;
            }
            if (last.cursorY + height + Gap <= m_pageSize)
            {
                page = static_cast<std::uint32_t>(m_pages.Size() - 1);
                x = last.cursorX;
                y = last.cursorY;
                last.cursorX += width + Gap;
                last.shelfHeight = std::max(last.shelfHeight, height);
                return true;
            }
        }
        if (m_pages.Size() >= 0xFFFF)
        {
            return false;
        }
        Page& fresh = m_pages.Emplace();
        fresh.pixels.Resize(static_cast<std::size_t>(m_pageSize) * m_pageSize * 4);
        // 빈 자리는 투명한 흰색이다. 가장자리를 Linear 로 읽어도 색이 어두워지지 않는다.
        for (std::size_t index = 0; index < fresh.pixels.Size(); index += 4)
        {
            fresh.pixels[index + 0] = std::byte{ 255 };
            fresh.pixels[index + 1] = std::byte{ 255 };
            fresh.pixels[index + 2] = std::byte{ 255 };
            fresh.pixels[index + 3] = std::byte{ 0 };
        }
        fresh.cursorX = Gap + width + Gap;
        fresh.cursorY = Gap;
        fresh.shelfHeight = height;
        fresh.dirty = true;
        page = static_cast<std::uint32_t>(m_pages.Size() - 1);
        x = Gap;
        y = Gap;
        return true;
    }

    std::uint32_t GlyphAtlas::GetPageSize() const
    {
        return m_pageSize;
    }

    std::uint32_t GlyphAtlas::GetPageCount() const
    {
        return static_cast<std::uint32_t>(m_pages.Size());
    }

    ArrayView<const std::byte> GlyphAtlas::GetPagePixels(std::uint32_t page) const
    {
        if (page >= m_pages.Size())
        {
            return {};
        }
        return ArrayView<const std::byte>(m_pages[page].pixels.Data(), m_pages[page].pixels.Size());
    }

    bool GlyphAtlas::IsPageDirty(std::uint32_t page) const
    {
        return page < m_pages.Size() && m_pages[page].dirty;
    }

    void GlyphAtlas::ClearPageDirty(std::uint32_t page)
    {
        if (page < m_pages.Size())
        {
            m_pages[page].dirty = false;
            m_pages[page].dirtyMinX = 0;
            m_pages[page].dirtyMinY = 0;
            m_pages[page].dirtyMaxX = 0;
            m_pages[page].dirtyMaxY = 0;
        }
    }

    void GlyphAtlas::GetPageDirtyRect(std::uint32_t page, std::uint32_t& x, std::uint32_t& y, std::uint32_t& width,
        std::uint32_t& height) const
    {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        if (page >= m_pages.Size() || false == m_pages[page].dirty)
        {
            return;
        }
        const Page& target = m_pages[page];
        x = target.dirtyMinX;
        y = target.dirtyMinY;
        width = target.dirtyMaxX - target.dirtyMinX;
        height = target.dirtyMaxY - target.dirtyMinY;
    }

    std::uint32_t GlyphAtlas::GetGlyphCount() const
    {
        return static_cast<std::uint32_t>(m_glyphs.Size());
    }

    void GlyphAtlas::Clear()
    {
        m_pages.Clear();
        m_glyphs.Clear();
    }

    namespace
    {
        constexpr char BakedMagic[4] = { 'J', 'A', 'T', 'L' };
        constexpr std::uint32_t BakedVersion = 1;
        // 머리: 표지 4 + 판 4 + 원본 해시 8 + 벌 1·빈칸 3 + 크기 4 + 퍼짐 4 + 페이지 크기 4 + 페이지 수 4 + 칸 수 4.
        constexpr std::size_t BakedHeaderSize = 40;
        constexpr std::size_t BakedPageHeaderSize = 12;
        constexpr std::size_t BakedGlyphSize = 24;

        template <typename T>
        void PutValue(Array<std::byte>& out, T value)
        {
            const std::size_t at = out.Size();
            out.Resize(at + sizeof(value));
            std::memcpy(out.Data() + at, &value, sizeof(value));
        }

        template <typename T>
        T TakeValue(const std::byte*& at)
        {
            T value;
            std::memcpy(&value, at, sizeof(value));
            at += sizeof(value);
            return value;
        }
    }

    std::uint64_t GlyphAtlas::HashFontSource(const std::byte* bytes, std::size_t size)
    {
        std::uint64_t hash = 0xCBF29CE484222325ull;
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<std::uint8_t>(bytes[index]);
            hash *= 0x100000001B3ull;
        }
        return hash;
    }

    void GlyphAtlas::Bake(const BakedAtlasStamp& stamp, Array<std::byte>& out) const
    {
        out.Clear();
        const std::size_t area = static_cast<std::size_t>(m_pageSize) * m_pageSize;
        out.Reserve(BakedHeaderSize + m_pages.Size() * (BakedPageHeaderSize + area) + m_glyphs.Size() * BakedGlyphSize);
        for (const char value : BakedMagic)
        {
            PutValue(out, value);
        }
        PutValue(out, BakedVersion);
        PutValue(out, stamp.sourceHash);
        PutValue(out, static_cast<std::uint8_t>(stamp.set));
        PutValue(out, std::uint8_t{ 0 });
        PutValue(out, std::uint16_t{ 0 });
        PutValue(out, stamp.pixelSize);
        PutValue(out, stamp.sdfSpread);
        PutValue(out, m_pageSize);
        PutValue(out, static_cast<std::uint32_t>(m_pages.Size()));
        PutValue(out, static_cast<std::uint32_t>(m_glyphs.Size()));
        for (const Page& page : m_pages)
        {
            PutValue(out, page.cursorX);
            PutValue(out, page.cursorY);
            PutValue(out, page.shelfHeight);
            // RGB 는 늘 255 라 알파만 싼다(4 배 작다).
            const std::size_t at = out.Size();
            out.Resize(at + area);
            for (std::size_t pixel = 0; pixel < area; ++pixel)
            {
                out[at + pixel] = page.pixels[pixel * 4 + 3];
            }
        }
        for (const auto& entry : m_glyphs)
        {
            const AtlasGlyph& glyph = entry.MappedValue;
            PutValue(out, entry.KeyValue);
            PutValue(out, glyph.page);
            PutValue(out, glyph.x);
            PutValue(out, glyph.y);
            PutValue(out, glyph.width);
            PutValue(out, glyph.height);
            PutValue(out, glyph.left);
            PutValue(out, glyph.top);
            PutValue(out, static_cast<std::uint8_t>(glyph.empty ? 1 : 0));
            PutValue(out, std::uint8_t{ 0 });
        }
    }

    bool GlyphAtlas::Restore(ArrayView<const std::byte> baked, const BakedAtlasStamp& expected)
    {
        if (baked.Size() < BakedHeaderSize || std::memcmp(baked.Data(), BakedMagic, sizeof(BakedMagic)) != 0)
        {
            return false;
        }
        const std::byte* at = baked.Data() + sizeof(BakedMagic);
        const auto version = TakeValue<std::uint32_t>(at);
        const auto sourceHash = TakeValue<std::uint64_t>(at);
        const auto set = TakeValue<std::uint8_t>(at);
        at += 3;
        const auto pixelSize = TakeValue<std::uint32_t>(at);
        const auto spread = TakeValue<std::uint32_t>(at);
        const auto pageSize = TakeValue<std::uint32_t>(at);
        const auto pageCount = TakeValue<std::uint32_t>(at);
        const auto glyphCount = TakeValue<std::uint32_t>(at);
        if (version != BakedVersion || sourceHash != expected.sourceHash || set != static_cast<std::uint8_t>(expected.set)
            || pixelSize != expected.pixelSize || spread != expected.sdfSpread || pageSize != m_pageSize)
        {
            return false;
        }
        const std::size_t area = static_cast<std::size_t>(pageSize) * pageSize;
        const std::size_t size = BakedHeaderSize + static_cast<std::size_t>(pageCount) * (BakedPageHeaderSize + area)
            + static_cast<std::size_t>(glyphCount) * BakedGlyphSize;
        if (baked.Size() != size)
        {
            return false;
        }
        Array<Page> pages;
        pages.Resize(pageCount);
        for (Page& page : pages)
        {
            page.cursorX = TakeValue<std::uint32_t>(at);
            page.cursorY = TakeValue<std::uint32_t>(at);
            page.shelfHeight = TakeValue<std::uint32_t>(at);
            if (page.cursorX > pageSize || page.cursorY > pageSize || page.shelfHeight > pageSize)
            {
                return false;
            }
            page.pixels.Resize(area * 4);
            for (std::size_t pixel = 0; pixel < area; ++pixel)
            {
                page.pixels[pixel * 4 + 0] = std::byte{ 255 };
                page.pixels[pixel * 4 + 1] = std::byte{ 255 };
                page.pixels[pixel * 4 + 2] = std::byte{ 255 };
                page.pixels[pixel * 4 + 3] = at[pixel];
            }
            at += area;
            // 통째로 올린다 - GPU 에는 아직 아무것도 없다.
            page.dirty = true;
            page.dirtyMinX = 0;
            page.dirtyMinY = 0;
            page.dirtyMaxX = pageSize;
            page.dirtyMaxY = pageSize;
        }
        Table<std::uint64_t, AtlasGlyph> glyphs;
        for (std::uint32_t index = 0; index < glyphCount; ++index)
        {
            const auto key = TakeValue<std::uint64_t>(at);
            AtlasGlyph glyph;
            glyph.page = TakeValue<std::uint16_t>(at);
            glyph.x = TakeValue<std::uint16_t>(at);
            glyph.y = TakeValue<std::uint16_t>(at);
            glyph.width = TakeValue<std::uint16_t>(at);
            glyph.height = TakeValue<std::uint16_t>(at);
            glyph.left = TakeValue<std::int16_t>(at);
            glyph.top = TakeValue<std::int16_t>(at);
            glyph.empty = TakeValue<std::uint8_t>(at) != 0;
            at += 1;
            // 칸이 페이지 밖을 가리키면 깨진 것이다.
            if (false == glyph.empty
                && (glyph.page >= pageCount || static_cast<std::uint32_t>(glyph.x) + glyph.width > pageSize || static_cast<std::uint32_t>(glyph.y) + glyph.height > pageSize))
            {
                return false;
            }
            if (false == glyphs.TryAdd(key, glyph))
            {
                return false;
            }
        }
        m_pages = std::move(pages);
        m_glyphs = std::move(glyphs);
        return true;
    }
}
