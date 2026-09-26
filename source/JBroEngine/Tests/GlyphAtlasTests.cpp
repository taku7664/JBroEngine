#include <JBro/Text/FontFace.h>
#include <JBro/Text/GlyphAtlas.h>

#include "TestFontNotoSansKR.generated.h"

#include <cmath>
#include <cstring>
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

    // **SDF 칸**(text-plan §5 의 4 단계). 거리장은 비트맵 상자보다 사방으로 퍼짐만큼 크고, 외곽선에서 128, 글자 안쪽은 그 위,
    // 퍼짐 밖(칸의 모서리)은 0 이다. 같은 글리프의 비트맵 칸과 키가 갈라 서로 덮지 않고, 두 번째 요청은 새로 뜨지 않는다.
    void TestSdfCellsCarryADistanceField()
    {
        const FontFace face = LoadTestFont();
        GlyphAtlas atlas;
        const GlyphIndex a = face.FindGlyph(U'A');
        constexpr std::uint32_t Size = 48;
        constexpr std::uint32_t Spread = 8;

        GlyphBitmapBox bitmap;
        Check(face.MeasureGlyphBitmap(a, static_cast<float>(Size), bitmap), "A measures at 48 px");
        AtlasGlyph sdf;
        Check(atlas.EnsureSdf(face, Size, Spread, a, sdf) == AtlasError::None && false == sdf.empty, "A gets an SDF cell");
        std::cout << "  [measure] 48 px A bitmap " << bitmap.width << "x" << bitmap.height << ", sdf cell " << sdf.width << "x"
                  << sdf.height << " at " << sdf.left << "," << sdf.top << std::endl;
        Check(std::abs(static_cast<int>(sdf.width) - (bitmap.width + 2 * static_cast<int>(Spread))) <= 2
                && std::abs(static_cast<int>(sdf.height) - (bitmap.height + 2 * static_cast<int>(Spread))) <= 2,
            "the field is the bitmap box grown by the spread on every side");
        Check(std::abs((sdf.left + static_cast<int>(Spread)) - bitmap.left) <= 1 && std::abs((sdf.top - static_cast<int>(Spread)) - bitmap.top) <= 1,
            "and it sits the spread outside the bitmap's corner");

        // 칸의 모서리는 글자에서 퍼짐보다 멀다. 가운데 줄에는 글자 안(128 위)과 밖(128 아래)이 다 있다.
        Check(AlphaAt(atlas, 0, sdf.x, sdf.y) == 0 && AlphaAt(atlas, 0, sdf.x + sdf.width - 1, sdf.y + sdf.height - 1) == 0,
            "the cell's corners are past the spread, at zero");
        std::uint8_t highest = 0;
        std::uint8_t lowest = 255;
        const std::uint32_t row = static_cast<std::uint32_t>(sdf.y) + sdf.height * 3 / 4;
        for (std::uint32_t x = sdf.x; x < static_cast<std::uint32_t>(sdf.x) + sdf.width; ++x)
        {
            highest = std::max(highest, AlphaAt(atlas, 0, x, row));
            lowest = std::min(lowest, AlphaAt(atlas, 0, x, row));
        }
        std::cout << "  [measure] leg row distance " << static_cast<int>(lowest) << ".." << static_cast<int>(highest) << std::endl;
        // 다리 굵기가 5 px 남짓이라 안쪽 깊이는 2 px 남짓(128 + 2 x 16)이다.
        Check(highest > 140 && lowest < 60, "a row through the legs runs from inside to well outside the edge");
        Check(RedAt(atlas, 0, sdf.x + sdf.width / 2, sdf.y + sdf.height / 2) == 255, "the field is stored as white with the distance in alpha");

        // 비트맵 칸과 섞여도 서로 다른 칸이다. 다시 물으면 같은 칸을 준다.
        AtlasGlyph bitmapCell;
        Check(atlas.Ensure(face, Size, a, bitmapCell) == AtlasError::None, "the bitmap A goes in too");
        Check(bitmapCell.x != sdf.x || bitmapCell.y != sdf.y, "the bitmap and the field get separate cells");
        Check(atlas.GetGlyphCount() == 2, "two cells for one glyph in two modes");
        atlas.ClearPageDirty(0);
        AtlasGlyph again;
        Check(atlas.EnsureSdf(face, Size, Spread, a, again) == AtlasError::None && again.x == sdf.x && again.y == sdf.y,
            "a known field keeps its cell");
        Check(false == atlas.IsPageDirty(0) && atlas.GetGlyphCount() == 2, "and is not drawn again");
        AtlasGlyph wider;
        Check(atlas.EnsureSdf(face, Size, Spread + 4, a, wider) == AtlasError::None && wider.width > sdf.width,
            "another spread is another, wider cell");

        AtlasGlyph space;
        Check(atlas.EnsureSdf(face, Size, Spread, face.FindGlyph(U' '), space) == AtlasError::None && space.empty,
            "the space has no field");
        AtlasGlyph refused;
        Check(atlas.EnsureSdf(face, Size, 0, a, refused) == AtlasError::InvalidPixelSize
                && atlas.EnsureSdf(face, Size, GlyphAtlas::MaxSdfSpread + 1, a, refused) == AtlasError::InvalidPixelSize,
            "a spread of zero or past the limit is refused");
    }

    // **미리 뜨기**(text-plan §3.6). ASCII 벌은 95 자이고 그중 공백은 칸 없이 기억된다. 완성형 벌은 폰트에 있는 한글만 더한다
    // (시험 폰트에는 서로 다른 음절 29 자가 있고 모두 완성형이다 - `뷁` 은 폰트에 없다). 미리 뜬 글자는 다시 물어도 새로 뜨지 않는다.
    void TestPrewarmFillsTheAtlasOnce()
    {
        const FontFace face = LoadTestFont();
        GlyphAtlas ascii;
        Check(ascii.Prewarm(face, PrewarmSet::Ascii, 32, 0) == 95, "the ASCII set is 95 glyphs");
        Check(ascii.Find(32, face.FindGlyph(U'A')) != nullptr && ascii.Find(32, face.FindGlyph(U'~')) != nullptr,
            "A and ~ are in the atlas without being asked for");
        Check(ascii.Prewarm(face, PrewarmSet::Ascii, 32, 0) == 0, "prewarming twice adds nothing");

        GlyphAtlas korean;
        const std::uint32_t warmed = korean.Prewarm(face, PrewarmSet::Ksx1001, 48, 8);
        std::cout << "  [measure] KS X 1001 prewarm of the test font: " << warmed << " SDF glyphs on " << korean.GetPageCount()
                  << " page(s)" << std::endl;
        Check(warmed == 95 + 29, "the Korean set adds the font's 29 syllables to ASCII");
        AtlasGlyph han;
        korean.ClearPageDirty(0);
        Check(korean.EnsureSdf(face, 48, 8, face.FindGlyph(U'\uD55C'), han) == AtlasError::None && false == han.empty
                && false == korean.IsPageDirty(0),
            "a prewarmed syllable is already there - asking for it draws nothing");
        Check(korean.Prewarm(face, PrewarmSet::None, 48, 8) == 0, "the empty set does nothing");
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

namespace
{
    // **미리 뜬 아틀라스를 싸고 되살린다**(D-232). 되살린 것은 뜬 것과 페이지·칸이 같고, 새 글자는 같은 자리에 이어 들어간다.
    // 표지가 다르거나 잘린 것은 되살리지 않고 지금 것을 건드리지 않는다.
    void TestABakedAtlasRestores()
    {
        const FontFace face = LoadTestFont();
        const std::uint64_t hash = GlyphAtlas::HashFontSource(reinterpret_cast<const std::byte*>(TestFontNotoSansKR), sizeof(TestFontNotoSansKR));
        BakedAtlasStamp stamp;
        stamp.sourceHash = hash;
        stamp.set = PrewarmSet::Ksx1001;
        stamp.pixelSize = 48;
        stamp.sdfSpread = 8;
        GlyphAtlas warmed;
        warmed.Prewarm(face, PrewarmSet::Ksx1001, 48, 8);
        Array<std::byte> baked;
        warmed.Bake(stamp, baked);
        const std::uint32_t bakedGlyphs = warmed.GetGlyphCount();
        Check(baked.Size() < static_cast<std::size_t>(warmed.GetPageCount()) * warmed.GetPageSize() * warmed.GetPageSize() * 2,
            "a baked page keeps one channel, not four");

        GlyphAtlas restored;
        Check(restored.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size()), stamp), "a baked atlas restores");
        Check(restored.GetPageCount() == warmed.GetPageCount() && restored.GetGlyphCount() == warmed.GetGlyphCount(),
            "with the same pages and cells");
        bool samePixels = true;
        for (std::uint32_t page = 0; page < warmed.GetPageCount(); ++page)
        {
            const ArrayView<const std::byte> a = warmed.GetPagePixels(page);
            const ArrayView<const std::byte> b = restored.GetPagePixels(page);
            samePixels = samePixels && a.Size() == b.Size() && std::memcmp(a.Data(), b.Data(), a.Size()) == 0;
            Check(restored.IsPageDirty(page), "every restored page waits to be uploaded");
        }
        Check(samePixels, "the restored pages are pixel for pixel the warmed ones");
        AtlasGlyph fromWarm;
        AtlasGlyph fromRestored;
        const GlyphIndex han = face.FindGlyph(U'\uD55C');
        Check(warmed.EnsureSdf(face, 48, 8, han, fromWarm) == AtlasError::None
                && restored.EnsureSdf(face, 48, 8, han, fromRestored) == AtlasError::None
                && fromWarm.page == fromRestored.page && fromWarm.x == fromRestored.x && fromWarm.y == fromRestored.y,
            "a prewarmed cell is found where it was baked");
        // 벌 밖의 새 글자(다른 크기)는 두 아틀라스에서 같은 자리에 선다 - 선반 자리가 함께 싸였다.
        Check(warmed.EnsureSdf(face, 20, 8, han, fromWarm) == AtlasError::None
                && restored.EnsureSdf(face, 20, 8, han, fromRestored) == AtlasError::None
                && fromWarm.page == fromRestored.page && fromWarm.x == fromRestored.x && fromWarm.y == fromRestored.y,
            "a new cell after restoring goes where it would have gone");

        // 표지·잘림·겹친 칸.
        const std::uint32_t kept = restored.GetGlyphCount();
        BakedAtlasStamp other = stamp;
        other.sourceHash ^= 1;
        Check(false == restored.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size()), other) && restored.GetGlyphCount() == kept,
            "another font's bake is refused and the atlas is left alone");
        other = stamp;
        other.pixelSize = 32;
        Check(false == restored.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size()), other), "another size is refused");
        other = stamp;
        other.set = PrewarmSet::Ascii;
        Check(false == restored.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size()), other), "another set is refused");
        Check(false == restored.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size() - 1), stamp), "a cut bake is refused");
        GlyphAtlas small(256);
        Check(false == small.Restore(ArrayView<const std::byte>(baked.Data(), baked.Size()), stamp), "another page size is refused");
        Array<std::byte> damaged = baked;
        // 그릴 것이 있는 첫 칸(빈 칸 표시가 0)의 페이지 번호를 페이지 수 밖으로 돌린다.
        const std::size_t firstGlyph = baked.Size() - static_cast<std::size_t>(bakedGlyphs) * 24;
        std::size_t drawn = firstGlyph;
        while (drawn < baked.Size() && baked[drawn + 22] != std::byte{ 0 })
        {
            drawn += 24;
        }
        Check(drawn < baked.Size(), "the bake has a drawn cell");
        const std::uint16_t badPage = 60000;
        std::memcpy(damaged.Data() + drawn + 8, &badPage, sizeof(badPage));
        GlyphAtlas fresh;
        Check(false == fresh.Restore(ArrayView<const std::byte>(damaged.Data(), damaged.Size()), stamp) && fresh.GetPageCount() == 0,
            "a cell outside the pages is refused");
    }
}

int RunGlyphAtlasTests()
{
    try
    {
        TestBitmapBoxMatchesTheOutline();
        TestEnsureRasterizesOnce();
        TestSdfCellsCarryADistanceField();
        TestPrewarmFillsTheAtlasOnce();
        TestPagesDoNotMoveCells();
        TestErrors();
        TestABakedAtlasRestores();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "glyph atlas tests passed\n";
    return 0;
}
