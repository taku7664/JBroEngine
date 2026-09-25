#include <JBro/Text/FontFace.h>
#include <JBro/Text/TextLayout.h>

#include "TestFontNotoSansKR.generated.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 텍스트 커널 1 단계(D-200, text-plan §5)의 테스트다. 기대값은 fontTools 로 시험 폰트를 따로 읽어 뽑았다
// (Tests/Data/Fonts/README.md). 레이아웃은 fontSize 1000 으로 돌려 픽셀이 곧 폰트 단위가 되게 한다 - 숫자를 그대로 대조한다.
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

    bool Near(float actual, float expected)
    {
        return std::fabs(actual - expected) <= 0.01f;
    }

    ArrayView<const char> Utf8(const char* text)
    {
        return ArrayView<const char>(text, std::strlen(text));
    }

    FontFace LoadTestFont()
    {
        FontFace face;
        const bool loaded = face.Load(ArrayView<const std::byte>(
            reinterpret_cast<const std::byte*>(TestFontNotoSansKR), sizeof(TestFontNotoSansKR)));
        Check(loaded, "the test font loads");
        return face;
    }

    LayoutOptions Unscaled()
    {
        LayoutOptions options;
        options.fontSize = 1000.0f;
        options.overflow = Overflow::Overflow;
        return options;
    }

    // 줄마다 글리프 수를 모아 기대와 비교한다.
    bool LineCounts(const TextLayout& layout, std::initializer_list<std::uint32_t> expected)
    {
        const ArrayView<const LineInfo> lines = layout.GetLines();
        if (lines.Size() != expected.size())
        {
            return false;
        }
        std::size_t index = 0;
        for (const std::uint32_t count : expected)
        {
            if (lines[index].glyphCount != count)
            {
                return false;
            }
            ++index;
        }
        return true;
    }

    void TestFaceReadsTheFont()
    {
        const FontFace face = LoadTestFont();
        const FontMetrics& metrics = face.GetMetrics();
        Check(metrics.unitsPerEm == 1000, "unitsPerEm is 1000");
        Check(metrics.ascent == 1160 && metrics.descent == -288 && metrics.lineGap == 0, "hhea metrics match fontTools");

        const GlyphIndex a = face.FindGlyph(U'A');
        const GlyphIndex v = face.FindGlyph(U'V');
        const GlyphIndex t = face.FindGlyph(U'T');
        const GlyphIndex o = face.FindGlyph(U'o');
        const GlyphIndex han = face.FindGlyph(U'한');
        Check(a != MissingGlyph && v != MissingGlyph && t != MissingGlyph && o != MissingGlyph && han != MissingGlyph,
            "latin and hangul glyphs are present");
        Check(face.FindGlyph(0xFFFD) == MissingGlyph, "the subset has no U+FFFD");
        Check(face.FindGlyph(U'뷁') == MissingGlyph, "a syllable outside the subset is missing");

        Check(face.GetAdvance(a) == 608 && face.GetAdvance(v) == 575 && face.GetAdvance(t) == 599 && face.GetAdvance(o) == 606,
            "latin advances match fontTools");
        Check(face.GetAdvance(face.FindGlyph(U' ')) == 224, "the space advance is 224");
        Check(face.GetAdvance(han) == 920, "the hangul advance is 920");

        // stb_truetype 이 GPOS 쌍 조정(형식 2)을 읽는다는 가정(text-plan §7)의 판정이다.
        Check(face.GetKerning(a, v) == -15, "GPOS class kerning A-V is -15");
        Check(face.GetKerning(v, a) == -15, "GPOS class kerning V-A is -15");
        Check(face.GetKerning(t, o) == -74, "GPOS class kerning T-o is -74");
        Check(face.GetKerning(a, face.FindGlyph(U' ')) == 0, "A-space does not kern");

        const GlyphBox box = face.GetGlyphBox(a);
        Check(false == box.empty && box.maxX > box.minX && box.maxY > box.minY, "A has an outline box");
        Check(face.GetGlyphBox(face.FindGlyph(U' ')).empty, "the space has no outline");
    }

    void TestFaceRejectsGarbageAndMoves()
    {
        FontFace face;
        const std::byte tiny[4] = {};
        Check(false == face.Load(ArrayView<const std::byte>(tiny, 4)), "four bytes are not a font");
        Check(false == face.IsLoaded(), "a failed load leaves nothing loaded");
        std::byte junk[64] = {};
        for (std::size_t index = 0; index < 64; ++index)
        {
            junk[index] = static_cast<std::byte>(index * 37 + 11);
        }
        Check(false == face.Load(ArrayView<const std::byte>(junk, 64)), "junk bytes are not a font");
        Check(face.FindGlyph(U'A') == MissingGlyph && face.GetAdvance(1) == 0 && face.GetKerning(1, 2) == 0,
            "an unloaded face answers nothing");

        FontFace source = LoadTestFont();
        FontFace moved(static_cast<FontFace&&>(source));
        Check(false == source.IsLoaded(), "the moved-from face is empty");
        Check(moved.IsLoaded() && moved.GetKerning(moved.FindGlyph(U'T'), moved.FindGlyph(U'o')) == -74,
            "the moved face still reads its bytes");
        FontFace assigned;
        assigned = static_cast<FontFace&&>(moved);
        Check(assigned.GetAdvance(assigned.FindGlyph(U'한')) == 920, "move assignment keeps the face readable");
    }

    void TestKerningIsAppliedAcrossTheRun()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;

        // 기존 엔진은 글자 묶음 하나씩 셰이핑해 이 조정이 한 번도 일어나지 않았다(text-plan §1.2 의 1 번).
        Check(layout.Build(Utf8("AV"), faces, Unscaled()) == LayoutError::None, "AV lays out");
        Check(layout.GetGlyphs().Size() == 2, "AV has two glyphs");
        Check(Near(layout.GetGlyphs()[0].x, 0.0f) && Near(layout.GetGlyphs()[1].x, 608.0f - 15.0f), "V moves left by the AV kern");
        Check(Near(layout.GetLines()[0].width, 608.0f - 15.0f + 575.0f), "the line width includes the kern");

        Check(layout.Build(Utf8("To"), faces, Unscaled()) == LayoutError::None, "To lays out");
        Check(Near(layout.GetGlyphs()[1].x, 599.0f - 74.0f), "o tucks under T");

        LayoutOptions spaced = Unscaled();
        spaced.letterSpacing = 10.0f;
        Check(layout.Build(Utf8("AV"), faces, spaced) == LayoutError::None, "spaced AV lays out");
        Check(Near(layout.GetGlyphs()[1].x, 608.0f + 10.0f - 15.0f), "letter spacing adds to the kerned advance");
        Check(Near(layout.GetLines()[0].width, 608.0f + 10.0f - 15.0f + 575.0f), "letter spacing does not trail the last glyph");

        // 공백은 글리프로 나오지 않지만 자리를 차지한다.
        Check(layout.Build(Utf8("A V"), faces, Unscaled()) == LayoutError::None, "A V lays out");
        Check(layout.GetGlyphs().Size() == 2, "the space is not a glyph");
        Check(Near(layout.GetGlyphs()[1].x, 608.0f + 224.0f), "the space advances the pen");

        // 절반 크기에서는 모든 값이 절반이다.
        LayoutOptions half = Unscaled();
        half.fontSize = 500.0f;
        Check(layout.Build(Utf8("To"), faces, half) == LayoutError::None, "half-size To lays out");
        Check(Near(layout.GetGlyphs()[1].x, (599.0f - 74.0f) * 0.5f), "kerning scales with the font size");
    }

    void TestWordWrap()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.overflow = Overflow::Wrap;
        options.boxWidth = 3000.0f;

        // hello = 2335, 공백 224, world = 2696(wo 커닝 -4). 기존 엔진은 글자마다 폭을 보고 끊어 hel|lo 가 될 수 있었다.
        Check(layout.Build(Utf8("hello world"), faces, options) == LayoutError::None, "hello world wraps");
        Check(LineCounts(layout, { 5, 5 }), "hello world breaks after the space");
        Check(Near(layout.GetLines()[0].width, 2335.0f), "the trailing space is not part of the first line");
        Check(Near(layout.GetLines()[1].width, 2696.0f), "the second line is world");
        Check(Near(layout.GetGlyphs()[5].x, 0.0f), "w starts the second line at x = 0");
        Check(layout.GetGlyphs()[5].sourceOffset == 6 && layout.GetLines()[1].sourceBegin == 6, "the second line starts at byte 6");

        // 넓으면 한 줄이다.
        options.boxWidth = 5255.0f;
        Check(layout.Build(Utf8("hello world"), faces, options) == LayoutError::None, "a wide box lays out");
        Check(LineCounts(layout, { 10 }), "exactly the full width is one line");

        // Overflow 는 상자를 무시한다.
        options.boxWidth = 3000.0f;
        options.overflow = Overflow::Overflow;
        Check(layout.Build(Utf8("hello world"), faces, options) == LayoutError::None, "overflow lays out");
        Check(LineCounts(layout, { 10 }), "overflow never wraps");

        // 기회가 없는 긴 단어는 글자에서 끊는다. 글자를 잃지 않고 줄마다 상자 안이다.
        options.overflow = Overflow::Wrap;
        Check(layout.Build(Utf8("hellohellohello"), faces, options) == LayoutError::None, "a long word lays out");
        Check(layout.GetGlyphs().Size() == 15 && layout.GetLines().Size() >= 3, "a long word breaks between letters");
        for (const LineInfo& line : layout.GetLines())
        {
            Check(line.width <= 3000.0f + 0.01f && line.glyphCount > 0, "every broken line fits and is not empty");
        }

        // 상자보다 넓은 글자 하나는 그 줄에 홀로 남는다(멈추지 않는다).
        options.boxWidth = 100.0f;
        Check(layout.Build(Utf8("AB"), faces, options) == LayoutError::None, "a box narrower than a glyph lays out");
        Check(LineCounts(layout, { 1, 1 }), "each oversized glyph gets its own line");
    }

    void TestHangulWrapModes()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.overflow = Overflow::Wrap;
        // 한글 = 1840, 공백 224, 세 = 920 → "한글 세" = 2984, "한글 세계" = 3904.
        options.boxWidth = 3034.0f;

        options.wrapMode = WrapMode::Word;
        Check(layout.Build(Utf8("한글 세계"), faces, options) == LayoutError::None, "word mode lays out hangul");
        Check(LineCounts(layout, { 2, 2 }), "word mode keeps each eojeol whole");

        options.wrapMode = WrapMode::Character;
        Check(layout.Build(Utf8("한글 세계"), faces, options) == LayoutError::None, "character mode lays out hangul");
        Check(LineCounts(layout, { 3, 1 }), "character mode breaks between syllables");

        // 어절이 상자보다 길면 Word 에서도 음절에서 끊는다.
        options.wrapMode = WrapMode::Word;
        options.boxWidth = 920.0f * 3.0f + 50.0f;
        Check(layout.Build(Utf8("안녕하세요"), faces, options) == LayoutError::None, "a long eojeol lays out");
        Check(LineCounts(layout, { 3, 2 }), "a long eojeol breaks between syllables");
    }

    void TestDecoding()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        const GlyphIndex han = face.FindGlyph(U'한');

        // 분해된 자모(ᄒ ᅡ ᆫ)와 반쯤 조합된 것(하 + ᆫ)이 음절 한 자가 된다.
        Check(layout.Build(Utf8("한"), faces, Unscaled()) == LayoutError::None, "decomposed jamo lay out");
        Check(layout.GetGlyphs().Size() == 1 && layout.GetGlyphs()[0].glyph == han, "L V T jamo compose into one syllable");
        Check(layout.Build(Utf8("한"), faces, Unscaled()) == LayoutError::None, "syllable plus trail lays out");
        Check(layout.GetGlyphs().Size() == 1 && layout.GetGlyphs()[0].glyph == han, "LV syllable plus T composes");
        Check(layout.Build(Utf8("한글"), faces, Unscaled()) == LayoutError::None, "two decomposed syllables lay out");
        Check(layout.GetGlyphs().Size() == 2 && layout.GetGlyphs()[1].glyph == face.FindGlyph(U'글'), "two decomposed syllables compose separately");

        // 잘못된 UTF-8 은 U+FFFD 이고, 이 폰트에는 그것이 없어 .notdef 로 그린다.
        Check(layout.Build(Utf8("\xFF"), faces, Unscaled()) == LayoutError::None, "an invalid byte lays out");
        Check(layout.GetGlyphs().Size() == 1 && layout.GetGlyphs()[0].glyph == MissingGlyph, "an invalid byte is one notdef");
        Check(layout.Build(Utf8("A\xE2\x82"), faces, Unscaled()) == LayoutError::None, "a truncated sequence lays out");
        Check(layout.GetGlyphs().Size() == 2, "a truncated sequence is one replacement");
        Check(layout.Build(Utf8("\xE2\x82" "A"), faces, Unscaled()) == LayoutError::None, "an interrupted sequence lays out");
        Check(layout.GetGlyphs().Size() == 2 && layout.GetGlyphs()[1].glyph == face.FindGlyph(U'A'),
            "an interrupted sequence keeps the next letter");
        Check(layout.Build(Utf8("\xED\xA0\x80"), faces, Unscaled()) == LayoutError::None, "an encoded surrogate lays out");
        Check(layout.GetGlyphs().Size() == 1 && layout.GetGlyphs()[0].glyph == MissingGlyph, "an encoded surrogate is one replacement");
        // C0 AF 는 '/' 를 겹쳐 쓴 것이다. 받아들이면 '/' 글리프가 나온다.
        Check(layout.Build(Utf8("\xC0\xAF"), faces, Unscaled()) == LayoutError::None, "an overlong sequence lays out");
        Check(layout.GetGlyphs().Size() == 1 && layout.GetGlyphs()[0].glyph == MissingGlyph, "an overlong slash is one replacement, not a slash");

        // 줄바꿈: LF, CRLF, CR 이 모두 한 번이다.
        Check(layout.Build(Utf8("A\r\nB"), faces, Unscaled()) == LayoutError::None, "CRLF lays out");
        Check(LineCounts(layout, { 1, 1 }), "CRLF is one line break");
        Check(layout.Build(Utf8("A\rB"), faces, Unscaled()) == LayoutError::None, "CR lays out");
        Check(LineCounts(layout, { 1, 1 }), "a lone CR is a line break");
        Check(layout.Build(Utf8("A\n\nB"), faces, Unscaled()) == LayoutError::None, "an empty line lays out");
        Check(LineCounts(layout, { 1, 0, 1 }), "an empty line is kept");
        Check(Near(layout.GetLines()[2].baseline, -2.0f * 1448.0f), "lines are one line height apart");

        Check(layout.Build(Utf8(""), faces, Unscaled()) == LayoutError::None, "empty text lays out");
        Check(layout.GetGlyphs().Size() == 0 && layout.GetLines().Size() == 0, "empty text has nothing");

        // 폰트에 없는 음절은 .notdef 로 자리를 지킨다.
        Check(layout.Build(Utf8("A뷁"), faces, Unscaled()) == LayoutError::None, "a missing syllable lays out");
        Check(layout.GetGlyphs().Size() == 2 && layout.GetGlyphs()[1].glyph == MissingGlyph, "a missing syllable is notdef");
        Check(Near(layout.GetGlyphs()[1].x, 608.0f), "the notdef sits after A");
    }

    void TestAlignment()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        const float width = 608.0f - 15.0f + 575.0f;
        const float lineHeight = 1160.0f + 288.0f;

        Check(layout.Build(Utf8("AV"), faces, options) == LayoutError::None, "baseline-left lays out");
        Check(Near(layout.GetGlyphs()[0].y, 0.0f) && Near(layout.GetMaxY(), 1160.0f) && Near(layout.GetMinY(), -288.0f),
            "baseline puts the first baseline at y = 0");
        Check(Near(layout.GetMinX(), 0.0f) && Near(layout.GetMaxX(), width), "left puts the block's left edge at x = 0");

        options.alignX = AlignX::Center;
        options.alignY = AlignY::Middle;
        Check(layout.Build(Utf8("AV"), faces, options) == LayoutError::None, "center-middle lays out");
        Check(Near(layout.GetGlyphs()[0].x, -width * 0.5f) && Near(layout.GetMinX(), -width * 0.5f), "center splits the width");
        Check(Near(layout.GetMaxY(), lineHeight * 0.5f) && Near(layout.GetMinY(), -lineHeight * 0.5f), "middle splits the height");

        options.alignX = AlignX::Right;
        options.alignY = AlignY::Top;
        Check(layout.Build(Utf8("AV\nA"), faces, options) == LayoutError::None, "right-top lays out");
        Check(Near(layout.GetMaxX(), 0.0f) && Near(layout.GetMaxY(), 0.0f), "right-top puts the corner at the origin");
        Check(Near(layout.GetGlyphs()[2].x, -608.0f), "a shorter line sticks to the right edge");
        Check(Near(layout.GetLines()[0].baseline, -1160.0f) && Near(layout.GetLines()[1].baseline, -1160.0f - lineHeight),
            "top puts the ascent at y = 0");

        options.alignY = AlignY::Bottom;
        Check(layout.Build(Utf8("AV\nA"), faces, options) == LayoutError::None, "bottom lays out");
        Check(Near(layout.GetMinY(), 0.0f) && Near(layout.GetMaxY(), 2.0f * lineHeight), "bottom puts the block's bottom at y = 0");

        // 상자가 있으면 블록은 상자다. 줄은 그 안에서 가운데로 간다.
        options.alignX = AlignX::Center;
        options.alignY = AlignY::Top;
        options.boxWidth = 4000.0f;
        options.boxHeight = 3000.0f;
        Check(layout.Build(Utf8("AV"), faces, options) == LayoutError::None, "a boxed center lays out");
        Check(Near(layout.GetMinX(), -2000.0f) && Near(layout.GetMaxX(), 2000.0f) && Near(layout.GetMinY(), -3000.0f),
            "the box is the block");
        Check(Near(layout.GetGlyphs()[0].x, -width * 0.5f), "the line is centered inside the box");
    }

    void TestClip()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        // 줄 높이 1448. 줄의 위쪽이 상자 안에 있으면 남는다(걸친 조각은 그리는 쪽이 자른다).
        options.boxHeight = 1448.0f - 100.0f;

        options.overflow = Overflow::Clip;
        Check(layout.Build(Utf8("A\nB\nC"), faces, options) == LayoutError::None, "clip lays out");
        Check(LineCounts(layout, { 1 }), "clip drops the lines that start below the box");

        options.boxHeight = 1448.0f + 100.0f;
        Check(layout.Build(Utf8("A\nB\nC"), faces, options) == LayoutError::None, "a clip that cuts the second line lays out");
        Check(LineCounts(layout, { 1, 1 }), "a line that starts inside the box is kept even if it overhangs");

        options.boxHeight = 2.0f * 1448.0f;
        Check(layout.Build(Utf8("A\nB\nC"), faces, options) == LayoutError::None, "a taller clip lays out");
        Check(LineCounts(layout, { 1, 1 }), "a line that starts exactly at the box bottom is dropped");

        options.boxHeight = 10.0f;
        Check(layout.Build(Utf8("A\nB"), faces, options) == LayoutError::None, "a box smaller than one line lays out");
        Check(LineCounts(layout, { 1 }), "the first line is always kept so its visible part can show");

        options.overflow = Overflow::Wrap;
        Check(layout.Build(Utf8("A\nB\nC"), faces, options) == LayoutError::None, "wrap with a box height lays out");
        Check(LineCounts(layout, { 1, 1, 1 }), "wrap keeps lines below the box");
    }

    void TestErrorsAndFaceChoice()
    {
        const FontFace face = LoadTestFont();
        const FontFace empty;
        TextLayout layout;

        const FontFace* none[] = { &empty };
        Check(layout.Build(Utf8("A"), none, Unscaled()) == LayoutError::NoFace, "no loaded face is an error");
        Check(layout.Build(Utf8("A"), ArrayView<const FontFace* const>(), Unscaled()) == LayoutError::NoFace, "no face at all is an error");

        const FontFace* faces[] = { &face };
        LayoutOptions options = Unscaled();
        options.fontSize = 0.0f;
        Check(layout.Build(Utf8("A"), faces, options) == LayoutError::InvalidFontSize, "size 0 is an error");
        options.fontSize = std::nanf("");
        Check(layout.Build(Utf8("A"), faces, options) == LayoutError::InvalidFontSize, "NaN size is an error");
        Check(layout.GetGlyphs().Size() == 0, "a failed build leaves nothing");

        // 열리지 않은 face 는 건너뛰고, 첫 번째로 열린 것이 기본이다.
        const FontFace* skipped[] = { &empty, nullptr, &face };
        Check(layout.Build(Utf8("A뷁"), skipped, Unscaled()) == LayoutError::None, "unloaded faces are skipped");
        Check(layout.GetGlyphs()[0].face == 2 && layout.GetGlyphs()[1].face == 2, "the first loaded face is the primary");
        Check(Near(layout.GetLines()[0].baseline, 0.0f) && Near(layout.GetMaxY(), 1160.0f), "line metrics come from the primary");
    }

    void TestRebuildKeepsStorage()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.overflow = Overflow::Wrap;
        options.boxWidth = 3000.0f;

        Check(layout.Build(Utf8("hello world hello world 한글 세계"), faces, options) == LayoutError::None, "a long text lays out");
        const std::size_t reserved = layout.GetReservedCapacity();
        Check(reserved > 0, "a long text reserves storage");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(layout.Build(Utf8("hello world"), faces, options) == LayoutError::None, "a shorter text lays out");
            // 새 배열로 갈아 끼웠다면 짧은 글에 맞는 더 작은 용량이 남는다. 주소 비교로는 이것을 잡지 못했다
            // (풀었다 다시 잡은 블록이 같은 주소로 올 수 있다 - 뮤테이션이 운에 따라 살았다).
            Check(layout.GetReservedCapacity() == reserved, "rebuilding a shorter text keeps every buffer it had");
        }
        Check(layout.Build(Utf8("hello world hello world 한글 세계"), faces, options) == LayoutError::None, "the long text lays out again");
        Check(layout.GetReservedCapacity() == reserved, "the same long text needs no more storage");
    }
}

int RunTextLayoutTests()
{
    try
    {
        TestFaceReadsTheFont();
        TestFaceRejectsGarbageAndMoves();
        TestKerningIsAppliedAcrossTheRun();
        TestWordWrap();
        TestHangulWrapModes();
        TestDecoding();
        TestAlignment();
        TestClip();
        TestErrorsAndFaceChoice();
        TestRebuildKeepsStorage();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "text layout tests passed\n";
    return 0;
}
