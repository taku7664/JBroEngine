#include <JBro/Text/FontFace.h>
#include <JBro/Text/GlyphAtlas.h>

#include "TestFontNotoSansKR.generated.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

// 글리프 아틀라스(D-200, text-plan §5 의 2 단계)의 CPU 쪽 테스트다. GPU 는 쓰지 않는다.
namespace
{
    using namespace JBro;
    using namespace JBro::Text;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    FontFace LoadTestFont()
    {
        FontFace face;
        Check(face.Load(ArrayView<const std::byte>(
                  reinterpret_cast<const std::byte*>(TestFontNotoSansKR), sizeof(TestFontNotoSansKR))),
            "the test font loads");
        return face;
    }

    std::uint8_t AlphaAt(const GlyphAtlas& atlas, std::uint32_t page, std::uint32_t x, std::uint32_t y)
    {
        const ArrayView<const std::byte> pixels = atlas.GetPagePixels(page);
        return static_cast<std::uint8_t>(pixels[(static_cast<std::size_t>(y) * atlas.GetPageSize() + x) * 4 + 3]);
    }

    std::uint8_t RedAt(const GlyphAtlas& atlas, std::uint32_t page, std::uint32_t x, std::uint32_t y)
    {
        const ArrayView<const std::byte> pixels = atlas.GetPagePixels(page);
        return static_cast<std::uint8_t>(pixels[(static_cast<std::size_t>(y) * atlas.GetPageSize() + x) * 4 + 0]);
    }

    void TestBitmapBoxMatchesTheOutline()
    {
        const FontFace face = LoadTestFont();
        const GlyphIndex a = face.FindGlyph(U'A');
        const GlyphBox outline = face.GetGlyphBox(a);
        GlyphBitmapBox box;
        Check(face.MeasureGlyphBitmap(a, 32.0f, box), "A measures at 32 px");
        const float scale = 32.0f / 1000.0f;
        // 비트맵은 외곽선을 픽셀 격자로 넓힌 것이다(바깥쪽 올림).
        Check(box.top == static_cast<std::int32_t>(std::ceil(outline.maxY * scale)), "the bitmap top is the rounded-up outline top");
        Check(box.left == static_cast<std::int32_t>(std::floor(outline.minX * scale)), "the bitmap left is the rounded-down outline left");
        Check(box.width == static_cast<std::int32_t>(std::ceil(outline.maxX * scale)) - box.left, "the bitmap width covers the outline");
        Check(box.height > 0 && box.width > 0, "A has a bitmap");

        GlyphBitmapBox space;
        Check(face.MeasureGlyphBitmap(face.FindGlyph(U' '), 32.0f, space) && space.width == 0 && space.height == 0,
            "the space has no bitmap");
    }

    void TestEnsureRasterizesOnce()
    {
        const FontFace face = LoadTestFont();
        GlyphAtlas atlas;
        const GlyphIndex a = face.FindGlyph(U'A');

        AtlasGlyph glyph;
        Check(atlas.Ensure(face, 32, a, glyph) == AtlasError::None, "A goes into the atlas");
        Check(false == glyph.empty && glyph.page == 0 && glyph.width > 0 && glyph.height > 0, "A gets a cell on page 0");
        Check(atlas.GetPageCount() == 1 && atlas.IsPageDirty(0), "the first glyph opens a dirty page");
        Check(atlas.GetPagePixels(0).Size() == static_cast<std::size_t>(1024) * 1024 * 4, "a page is 1024x1024 RGBA8");

        // 칸 안에는 커버리지가, 칸 밖(틈)에는 투명한 흰색이 있다.
        std::uint32_t covered = 0;
        const std::uint32_t right = static_cast<std::uint32_t>(glyph.x) + glyph.width;
        const std::uint32_t bottom = static_cast<std::uint32_t>(glyph.y) + glyph.height;
        for (std::uint32_t y = glyph.y; y < bottom; ++y)
        {
            for (std::uint32_t x = glyph.x; x < right; ++x)
            {
                covered += AlphaAt(atlas, 0, x, y) > 128 ? 1u : 0u;
                Check(RedAt(atlas, 0, x, y) == 255, "glyph pixels are white so the tint is the text colour");
            }
        }
        Check(covered > static_cast<std::uint32_t>(glyph.width) * glyph.height / 8, "A covers a good part of its cell");
        Check(AlphaAt(atlas, 0, glyph.x - 1, glyph.y) == 0 && AlphaAt(atlas, 0, glyph.x + glyph.width, glyph.y) == 0,
            "the gap around a cell is transparent");
        Check(RedAt(atlas, 0, glyph.x + glyph.width + 5, glyph.y) == 255, "empty atlas space is transparent white");

        atlas.ClearPageDirty(0);
        AtlasGlyph again;
        Check(atlas.Ensure(face, 32, a, again) == AtlasError::None, "A again");
        Check(again.x == glyph.x && again.y == glyph.y && atlas.GetGlyphCount() == 1, "a known glyph keeps its cell");
        Check(false == atlas.IsPageDirty(0), "a known glyph does not dirty the page");
        const AtlasGlyph* found = atlas.Find(32, a);
        Check(found != nullptr && found->x == glyph.x, "Find sees the cell");
        Check(atlas.Find(33, a) == nullptr, "another size is another cell");

        AtlasGlyph bigger;
        Check(atlas.Ensure(face, 64, a, bigger) == AtlasError::None && bigger.height > glyph.height, "64 px A is taller");
        Check(atlas.IsPageDirty(0) && atlas.GetGlyphCount() == 2, "a new size is a new dirty cell");

        AtlasGlyph space;
        atlas.ClearPageDirty(0);
        Check(atlas.Ensure(face, 32, face.FindGlyph(U' '), space) == AtlasError::None && space.empty, "the space is an empty entry");
        Check(false == atlas.IsPageDirty(0) && atlas.GetGlyphCount() == 3, "an empty glyph is remembered without a cell");

        atlas.Clear();
        Check(atlas.GetPageCount() == 0 && atlas.GetGlyphCount() == 0 && atlas.Find(32, a) == nullptr, "Clear drops everything");
    }

    void TestPagesDoNotMoveCells()
    {
        const FontFace face = LoadTestFont();
        // 작은 페이지로 넘치게 한다. 한글 32 px 칸은 30 px 남짓이라 64 px 페이지에 넷이 들어가지 않는다.
        GlyphAtlas atlas(64);
        const char32_t syllables[] = { U'가', U'나', U'다', U'라', U'마', U'바', U'사', U'아' };
        AtlasGlyph first;
        Check(atlas.Ensure(face, 32, face.FindGlyph(syllables[0]), first) == AtlasError::None, "the first syllable goes in");
        for (std::size_t index = 1; index < 8; ++index)
        {
            AtlasGlyph glyph;
            Check(atlas.Ensure(face, 32, face.FindGlyph(syllables[index]), glyph) == AtlasError::None, "each syllable goes in");
            Check(glyph.x + glyph.width <= 64 && glyph.y + glyph.height <= 64, "every cell fits inside its page");
        }
        Check(atlas.GetPageCount() >= 2, "a full page opens another");
        const AtlasGlyph* stillFirst = atlas.Find(32, face.FindGlyph(syllables[0]));
        Check(stillFirst != nullptr && stillFirst->page == 0 && stillFirst->x == first.x && stillFirst->y == first.y,
            "the first cell does not move when a page is added");
        Check(atlas.IsPageDirty(atlas.GetPageCount() - 1), "the new page is dirty");

        // 칸끼리 겹치지 않는다.
        for (std::size_t left = 0; left < 8; ++left)
        {
            const AtlasGlyph* a = atlas.Find(32, face.FindGlyph(syllables[left]));
            for (std::size_t right = left + 1; right < 8; ++right)
            {
                const AtlasGlyph* b = atlas.Find(32, face.FindGlyph(syllables[right]));
                // 이웃 칸 사이에 한 픽셀 틈이 있다. Linear 샘플링이 옆 칸 가장자리를 읽지 않게 하는 틈이다.
                const bool apart = a->page != b->page
                    || a->x + a->width + 1 <= b->x || b->x + b->width + 1 <= a->x
                    || a->y + a->height + 1 <= b->y || b->y + b->height + 1 <= a->y;
                Check(apart, "cells never overlap and keep a one-pixel gap");
            }
        }
    }

    void TestErrors()
    {
        const FontFace face = LoadTestFont();
        const FontFace empty;
        GlyphAtlas atlas;
        AtlasGlyph glyph;
        Check(atlas.Ensure(empty, 32, 1, glyph) == AtlasError::FaceNotLoaded, "an unloaded face is refused");
        Check(atlas.Ensure(face, 0, face.FindGlyph(U'A'), glyph) == AtlasError::InvalidPixelSize, "size 0 is refused");
        Check(atlas.Ensure(face, GlyphAtlas::MaxPixelSize + 1, face.FindGlyph(U'A'), glyph) == AtlasError::InvalidPixelSize,
            "a size over the cap is refused");
        GlyphAtlas tiny(16);
        Check(tiny.Ensure(face, 64, face.FindGlyph(U'한'), glyph) == AtlasError::GlyphTooLarge, "a glyph bigger than a page is refused");
        Check(tiny.GetPageCount() == 0, "a refused glyph opens no page");
    }
}

int RunGlyphAtlasTests()
{
    try
    {
        TestBitmapBoxMatchesTheOutline();
        TestEnsureRasterizesOnce();
        TestPagesDoNotMoveCells();
        TestErrors();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "glyph atlas tests passed\n";
    return 0;
}
