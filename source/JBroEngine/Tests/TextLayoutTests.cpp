#include <JBro/Text/FontFace.h>
#include <JBro/Text/TextLayout.h>

#include "TestFontNotoSansKR.generated.h"
#include "TestFontNotoSansKRLatin.generated.h"
#include "TestFontNotoSansKRExtension.generated.h"
#include "TestFontNotoSansKRXPlacement.generated.h"
#include "TestFontNotoSansKRMarks.generated.h"
#include "TestFontNotoSansKRMarksExtension.generated.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

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

    // **stb 가 건너뛰던 GPOS 모양**(text-plan §7). 같은 서브셋을 확장 조회(형식 9)로 감싼 것과, 쌍 조정의 첫 값 형식을
    // XPlacement|XAdvance 로 넓힌 것이다(MakeGposVariants.py). stb 는 둘 다 커닝을 0 으로 읽었다. 우리 읽기는 ASCII 의 모든 쌍에서
    // 원본과 같은 값을 준다.
    void TestKerningSurvivesGposShapesStbSkipped()
    {
        const FontFace original = LoadTestFont();
        FontFace extension;
        FontFace widened;
        Check(extension.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKRExtension),
                  sizeof(TestFontNotoSansKRExtension))),
            "the extension-lookup font loads");
        Check(widened.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKRXPlacement),
                  sizeof(TestFontNotoSansKRXPlacement))),
            "the widened value format font loads");
        Check(extension.GetKerning(extension.FindGlyph(U'A'), extension.FindGlyph(U'V')) == -15
                && extension.GetKerning(extension.FindGlyph(U'T'), extension.FindGlyph(U'o')) == -74,
            "kerning inside extension lookups is read");
        Check(widened.GetKerning(widened.FindGlyph(U'A'), widened.FindGlyph(U'V')) == -15
                && widened.GetKerning(widened.FindGlyph(U'T'), widened.FindGlyph(U'o')) == -74,
            "kerning behind an X placement is read");
        std::uint32_t kerned = 0;
        for (char32_t left = 0x21; left <= 0x7E; ++left)
        {
            for (char32_t right = 0x21; right <= 0x7E; ++right)
            {
                const std::int32_t expected = original.GetKerning(original.FindGlyph(left), original.FindGlyph(right));
                kerned += expected != 0 ? 1u : 0u;
                if (extension.GetKerning(extension.FindGlyph(left), extension.FindGlyph(right)) != expected
                    || widened.GetKerning(widened.FindGlyph(left), widened.FindGlyph(right)) != expected)
                {
                    Check(false, "every ASCII pair kerns the same in all three shapes");
                }
            }
        }
        std::cout << "  [measure] kerned ASCII pairs in the test font: " << kerned << std::endl;
        Check(kerned > 100, "the comparison covers the font's kerned pairs");
    }

    // **결합 표시**(text-plan §7). 시험 폰트는 받침 A(폭 608)·e(폭 554)의 앵커가 (폭의 절반, 800), U+0301 의 앵커가 (-200, 600) 이다
    // (MakeMarkFont.py). 그러면 표시는 받침 원점에서 (폭/2 + 200, 200) 에 붙는다. 표시는 폭이 없어 다음 글자의 자리를 바꾸지 않고,
    // 앵커가 없는 받침(B)에서는 받침의 전진 폭 끝에 선다. 줄바꿈은 표시 앞에서 일어나지 않는다.
    void TestCombiningMarksAttachToTheirBase()
    {
        FontFace marks;
        FontFace wrapped;
        Check(marks.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKRMarks),
                  sizeof(TestFontNotoSansKRMarks))),
            "the mark font loads");
        Check(wrapped.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKRMarksExtension),
                  sizeof(TestFontNotoSansKRMarksExtension))),
            "the extension-wrapped mark font loads");
        const GlyphIndex acute = marks.FindGlyph(U'\u0301');
        Check(acute != MissingGlyph && marks.GetAdvance(acute) == 0, "the combining acute is in the font and has no advance");
        std::int32_t dx = 0;
        std::int32_t dy = 0;
        Check(marks.GetMarkAttachment(marks.FindGlyph(U'A'), acute, dx, dy) && dx == 504 && dy == 200,
            "the acute sits on A at the anchor difference");
        Check(marks.GetMarkAttachment(marks.FindGlyph(U'e'), acute, dx, dy) && dx == 477 && dy == 200,
            "and on e at its own anchor");
        Check(wrapped.GetMarkAttachment(wrapped.FindGlyph(U'A'), wrapped.FindGlyph(U'\u0301'), dx, dy) && dx == 504 && dy == 200,
            "a mark lookup inside an extension lookup is read");
        Check(false == marks.GetMarkAttachment(marks.FindGlyph(U'B'), acute, dx, dy) && dx == 0 && dy == 0,
            "a base without an anchor has no attachment");
        Check(false == marks.GetMarkAttachment(marks.FindGlyph(U'A'), marks.FindGlyph(U'e'), dx, dy),
            "a glyph that is not a mark has no attachment");

        const FontFace* faces[] = { &marks };
        TextLayout layout;
        const LayoutOptions options = Unscaled();
        // AV 는 커닝 쌍이다(-15). 표시가 끼어도 V 는 A 와 커닝한다.
        Check(layout.Build(Utf8("AV"), faces, options) == LayoutError::None, "AV lays out");
        const float bAfterA = layout.GetGlyphs()[1].x;
        const float plainWidth = layout.GetLines()[0].width;
        Check(Near(bAfterA, 608.0f - 15.0f), "the subset keeps the A-V kerning");
        Check(layout.Build(Utf8("A\xCC\x81" "V"), faces, options) == LayoutError::None, "A + acute + V lays out");
        Check(layout.GetGlyphs().Size() == 3 && layout.GetGlyphs()[1].glyph == acute, "the mark is its own glyph");
        const PositionedGlyph& base = layout.GetGlyphs()[0];
        const PositionedGlyph& mark = layout.GetGlyphs()[1];
        Check(Near(mark.x - base.x, 504.0f) && Near(mark.y - base.y, 200.0f), "the mark is placed at the anchor");
        Check(mark.sourceOffset == 1 && layout.GetGlyphs()[2].sourceOffset == 3, "the mark keeps its source bytes");
        Check(Near(layout.GetGlyphs()[2].x, bAfterA) && Near(layout.GetLines()[0].width, plainWidth),
            "the mark neither moves the next letter nor widens the line");
        Check(layout.Build(Utf8("A\xCC\x81\xCC\x81"), faces, options) == LayoutError::None
                && Near(layout.GetGlyphs()[2].x - layout.GetGlyphs()[0].x, 504.0f)
                && Near(layout.GetGlyphs()[2].y - layout.GetGlyphs()[0].y, 200.0f),
            "a second mark takes the base's anchor, not the first mark's");

        // 앵커가 없는 받침에서는 받침의 끝이다. 표시가 둘이면 같은 받침에 붙는다(mark-to-mark 는 읽지 않는다).
        Check(layout.Build(Utf8("B\xCC\x81\xCC\x81"), faces, options) == LayoutError::None, "B + two acutes lays out");
        const float bAdvance = static_cast<float>(marks.GetAdvance(marks.FindGlyph(U'B')));
        Check(layout.GetGlyphs().Size() == 3 && Near(layout.GetGlyphs()[1].x, bAdvance) && Near(layout.GetGlyphs()[1].y, layout.GetGlyphs()[0].y),
            "without an anchor the mark stands at the end of its base");
        Check(Near(layout.GetGlyphs()[2].x, bAdvance), "a second mark attaches to the same base");

        // 가운데 정렬로 줄이 옮겨져도 표시는 받침과 같이 옮겨진다.
        LayoutOptions centered = options;
        centered.alignX = AlignX::Center;
        Check(layout.Build(Utf8("A\xCC\x81"), faces, centered) == LayoutError::None, "a centred mark lays out");
        Check(Near(layout.GetGlyphs()[1].x - layout.GetGlyphs()[0].x, 504.0f), "alignment moves the mark with its base");
        // 절반 크기에서는 앵커 거리도 절반이다.
        LayoutOptions half = options;
        half.fontSize = 500.0f;
        Check(layout.Build(Utf8("A\xCC\x81"), faces, half) == LayoutError::None
                && Near(layout.GetGlyphs()[1].x - layout.GetGlyphs()[0].x, 252.0f)
                && Near(layout.GetGlyphs()[1].y - layout.GetGlyphs()[0].y, 100.0f),
            "the anchor offset scales with the font size");

        // 글자마다 끊는 모드에서 받침 하나의 폭이면, 줄은 받침 앞에서만 바뀌고 표시는 받침의 줄에 남는다.
        LayoutOptions narrow = options;
        narrow.overflow = Overflow::Wrap;
        narrow.wrapMode = WrapMode::Character;
        narrow.boxWidth = 560.0f;
        Check(layout.Build(Utf8("e\xCC\x81" "e\xCC\x81"), faces, narrow) == LayoutError::None, "two marked letters wrap");
        Check(LineCounts(layout, { 2, 2 }), "a line never starts with a mark");
        Check(Near(layout.GetGlyphs()[3].x - layout.GetGlyphs()[2].x, 477.0f) && layout.GetGlyphs()[3].line == 1,
            "the second mark follows its base onto the second line");

        // 받침이 없으면(줄 머리) 보통 글자다.
        Check(layout.Build(Utf8("\xCC\x81" "A"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 2,
            "a mark with no base still draws");
    }

    // **옛한글 자모는 폰트의 GSUB 로 한 음절이 된다**(D-238). 시험 폰트는 Windows 의 맑은 고딕(`malgun.ttf`, 13,457,164 바이트, em 2048)이다 -
    // 배포할 수 없는 폰트라 시험 데이터로 넣지 않고 설치된 것을 읽는다(없거나 판이 다르면 건너뛴다). 기대 글리프는 같은 파일을 DirectWrite
    // (`IDWriteTextAnalyzer::GetGlyphs`, ko-KR)로 모양 잡아 뽑았다. 바뀐 가운뎃소리·끝소리는 폭이 0 이라 첫소리 자리에 겹친다.
    void TestOldHangulJoinsThroughGsub()
    {
        std::ifstream in("C:/Windows/Fonts/malgun.ttf", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() != 13457164u)
        {
            std::cout << "  [skip] Malgun Gothic (13,457,164 bytes) is not installed; old hangul shaping not verified" << std::endl;
            return;
        }
        FontFace malgun;
        Check(malgun.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size())), "Malgun Gothic loads");
        Check(malgun.HasHangulJamoShaping(), "Malgun Gothic has the old hangul jamo features");
        const FontFace subset = LoadTestFont();
        Check(false == subset.HasHangulJamoShaping(), "the Noto subset has none");

        struct Case
        {
            char32_t input[3];
            std::size_t count;
            GlyphIndex expected[3];
            const char* message;
        };
        const Case cases[] = {
            { { 0x1100, 0x119E }, 2, { 20668, 21415 }, "kiyeok + arae-a" },
            { { 0x1100, 0x119E, 0x11A8 }, 3, { 20669, 21416, 21481 }, "kiyeok + arae-a + kiyeok" },
            { { 0x1112, 0x119E, 0x11AB }, 3, { 20759, 21416, 21484 }, "hieuh + arae-a + nieun" },
            { { 0x1100, 0x1161, 0x11F0 }, 3, { 20667, 21294, 21553 }, "a modern syllable with the old trail yesieung" },
            { { 0x110B, 0x1161, 0x11EB }, 3, { 20722, 21294, 21548 }, "ieung + a + pansios" },
            { { 0xA960, 0x1161 }, 2, { 21146, 21293 }, "an extended-A lead" },
            { { 0x1100, 0xD7B0 }, 2, { 20670, 21435 }, "an extended-B vowel" },
            { { 0x1100, 0x1161, 0xD7CB }, 3, { 20667, 21294, 21569 }, "an extended-B trail" },
            { { 0x41, 0x119E }, 2, { 36, 3139 }, "a vowel without a lead before it is left alone" },
        };
        for (const Case& item : cases)
        {
            GlyphIndex glyphs[3] = {};
            for (std::size_t index = 0; index < item.count; ++index)
            {
                glyphs[index] = malgun.FindGlyph(item.input[index]);
            }
            Check(malgun.ShapeHangulJamo(glyphs, item.count), "shaping runs");
            for (std::size_t index = 0; index < item.count; ++index)
            {
                if (glyphs[index] != item.expected[index])
                {
                    std::cout << "  " << item.message << ": glyph " << index << " is " << glyphs[index] << ", DirectWrite gives "
                              << item.expected[index] << std::endl;
                    Check(false, "the shaped glyph matches DirectWrite");
                }
            }
        }
        Check(malgun.GetAdvance(20669) == 2048 && malgun.GetAdvance(21416) == 0 && malgun.GetAdvance(21481) == 0,
            "the shaped lead keeps the full width and the vowel and trail have none");

        // 현대 자모만의 음절(`ᄇ ᅳ ᆼ`)은 표에 넣지 않는다 - DirectWrite 는 합칠 수 있는 음절에 이 기능을 걸지 않고(글리프 2993·3096·3164),
        // 이 함수는 받은 것에 조회를 다 건다. 그 가름은 레이아웃이 한다: 현대 음절은 산술로 합쳐 이 함수로 오지 않는다(아래 `ᄇ ᅳ ᆼ` → U+BE21).
        // 레이아웃: 한 음절은 첫 글리프 하나의 폭이고 나머지는 그 자리에 붙는다. em 2048 이라 fontSize 2048 이면 픽셀이 곧 폰트 단위다.
        const FontFace* faces[] = { &malgun };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.fontSize = 2048.0f;
        Check(layout.Build(Utf8("\u1100\u119E\u11A8\u1100\u119E\u11A8"), faces, options) == LayoutError::None, "two old syllables lay out");
        const ArrayView<const PositionedGlyph> glyphs = layout.GetGlyphs();
        Check(glyphs.Size() == 6 && glyphs[0].glyph == 20669 && glyphs[1].glyph == 21416 && glyphs[2].glyph == 21481,
            "each jamo keeps its own shaped glyph");
        // DirectWrite 와 같이 글리프 자리는 앞 글리프들의 폭 합이다: 가운뎃소리·끝소리는 첫소리 끝(2048)에 서고, 외곽선이 왼쪽(음수)으로
        // 뻗어 첫소리 칸 안에 그려진다(가운뎃소리 21416 은 -1124..-810 → 924..1238). 다음 음절도 같은 2048 에서 시작한다.
        Check(Near(glyphs[1].x, glyphs[0].x + 2048.0f) && Near(glyphs[2].x, glyphs[0].x + 2048.0f) && Near(glyphs[3].x, glyphs[0].x + 2048.0f),
            "the vowel and trail stand where the lead ends, like the next syllable");
        const GlyphBox vowelBox = malgun.GetGlyphBox(21416);
        Check(false == vowelBox.empty && vowelBox.minX + 2048 >= 0 && vowelBox.maxX + 2048 <= 2048,
            "and the vowel's outline reaches back into the lead's cell");
        Check(Near(layout.GetLines()[0].width, 4096.0f), "two old syllables are two cells wide");
        // 자간은 음절 사이에만 든다 - 뒤 자모는 첫 글리프에 붙은 것이라 자간을 받지 않는다.
        LayoutOptions spaced = options;
        spaced.letterSpacing = 100.0f;
        Check(layout.Build(Utf8("ᄀᆞᆨᄀᆞᆨ"), faces, spaced) == LayoutError::None
                && Near(layout.GetGlyphs()[1].x, layout.GetGlyphs()[0].x + 2048.0f) && Near(layout.GetGlyphs()[2].x, layout.GetGlyphs()[0].x + 2048.0f)
                && Near(layout.GetGlyphs()[3].x, layout.GetGlyphs()[0].x + 2148.0f),
            "letter spacing goes between syllables, not between the jamo of one");
        Check(glyphs[0].sourceOffset == 0 && glyphs[1].sourceOffset == 3 && glyphs[2].sourceOffset == 6 && glyphs[3].sourceOffset == 9,
            "each jamo keeps its source bytes");

        // 받침 없는 현대 음절 + 옛 끝소리는 자모로 풀어 한 음절로 모은다(DirectWrite 는 이것을 모으지 않는다).
        Check(layout.Build(Utf8("\uAC00\u11F0"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 3
                && layout.GetGlyphs()[0].glyph == 20667 && layout.GetGlyphs()[1].glyph == 21294 && layout.GetGlyphs()[2].glyph == 21553
                && layout.GetGlyphs()[2].sourceOffset == 3 && Near(layout.GetLines()[0].width, 2048.0f),
            "a precomposed syllable with an old trail is decomposed and joined");
        // 현대 자모만이면 예전처럼 음절 하나다.
        Check(layout.Build(Utf8("\u1107\u1173\u11BC"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 1
                && layout.GetGlyphs()[0].glyph == malgun.FindGlyph(0xBE21),
            "modern jamo still compose into one syllable");

        // 글자마다 끊어도 음절 가운데에서는 끊지 않는다.
        LayoutOptions narrow = options;
        narrow.overflow = Overflow::Wrap;
        narrow.wrapMode = WrapMode::Character;
        narrow.boxWidth = 2100.0f;
        Check(layout.Build(Utf8("\u1100\u119E\u11A8\u1112\u119E\u11AB"), faces, narrow) == LayoutError::None, "old syllables wrap");
        Check(LineCounts(layout, { 3, 3 }), "a line never breaks inside an old syllable");

        // 기능이 없는 폰트는 예전처럼 자모마다 따로 선다.
        const FontFace* plain[] = { &subset };
        Check(layout.Build(Utf8("\u1100\u119E"), plain, Unscaled()) == LayoutError::None && layout.GetGlyphs().Size() == 2
                && layout.GetGlyphs()[1].x > layout.GetGlyphs()[0].x,
            "a font without the features keeps each jamo on its own");
    }

    // **리치 텍스트**(D-221). 태그는 글자로 나오지 않고 뒤 글자에 색과 크기를 붙인다. 끄면 태그도 글자다. `<<` 는 `<` 한 글자이고,
    // 모르는 태그·틀린 태그·짝 없는 닫는 태그·아홉째 겹은 글자로 보인다. 크기가 섞인 줄은 가장 큰 글자로 줄 높이와 기준선을 잰다.
    // 시험 폰트: em 1000, 올림 1160, 줄 높이 1448(1160 + 288), `A` 폭 608, `AV` 커닝 -15.
    void TestRichTextMarkup()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();

        Check(layout.Build(Utf8("<color=#FF0000>A"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 16,
            "without richText a tag is text");
        options.richText = true;
        Check(layout.Build(Utf8("<color=#FF000080>A</color>B"), faces, options) == LayoutError::None, "a coloured A lays out");
        Check(layout.GetGlyphs().Size() == 2, "the tags draw nothing");
        Check(layout.GetGlyphs()[0].hasColor && layout.GetGlyphs()[0].color == 0x800000FFu, "the A takes the tag's colour and alpha");
        Check(false == layout.GetGlyphs()[1].hasColor, "and the B after the closing tag has none");
        Check(layout.GetGlyphs()[0].sourceOffset == 17 && layout.GetGlyphs()[1].sourceOffset == 26,
            "glyphs keep the byte offsets of their letters");
        Check(Near(layout.GetGlyphs()[1].x, 608.0f + static_cast<float>(face.GetKerning(face.FindGlyph(U'A'), face.FindGlyph(U'B')))),
            "and the B follows the A as if the tags were not there");
        Check(layout.Build(Utf8("<color=#00FF00>A</color>"), faces, options) == LayoutError::None
                && layout.GetGlyphs()[0].color == 0xFF00FF00u, "a colour without alpha is opaque");

        // 틀린 것은 글자로 보인다.
        Check(layout.Build(Utf8("a<<b"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 3
                && layout.GetGlyphs()[1].glyph == face.FindGlyph(U'<'), "<< is one <");
        Check(layout.Build(Utf8("</color>"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 8,
            "a closing tag with nothing open is text");
        Check(layout.Build(Utf8("<u>x"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 4, "an unknown tag is text");
        Check(layout.Build(Utf8("<color=#FF00>x"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 14,
            "a short colour is text");
        Check(layout.Build(Utf8("<size=0>x"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 9,
            "a zero size is text");
        Check(layout.Build(Utf8("<size=12px>x"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 12,
            "a size with a unit is text");
        Check(layout.Build(Utf8("a<b"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 3, "an unclosed < is text");
        {
            constexpr char tag[] = "<color=#112233>";
            constexpr std::size_t tagLength = sizeof(tag) - 1;
            char nested[tagLength * 9 + 1] = {};
            for (std::size_t depth = 0; depth < 9; ++depth)
            {
                std::memcpy(nested + depth * tagLength, tag, tagLength);
            }
            nested[tagLength * 9] = 'x';
            Check(layout.Build(ArrayView<const char>(nested, sizeof(nested)), faces, options) == LayoutError::None
                    && layout.GetGlyphs().Size() == 15 + 1, "the ninth nested tag is text");
        }

        // 크기: 뒤 글자의 전진 폭과 커닝이 그 크기로 재지고, 겹친 태그는 안쪽이 이긴다.
        Check(layout.Build(Utf8("A<size=2000>A</size>A"), faces, options) == LayoutError::None, "a sized A lays out");
        Check(Near(layout.GetGlyphs()[1].size, 2000.0f) && Near(layout.GetGlyphs()[0].size, 1000.0f) && Near(layout.GetGlyphs()[2].size, 1000.0f),
            "only the A inside the tag is 2000 px");
        Check(Near(layout.GetGlyphs()[2].x - layout.GetGlyphs()[1].x,
                1216.0f + static_cast<float>(face.GetKerning(face.FindGlyph(U'A'), face.FindGlyph(U'A')))),
            "and it advances twice as far");
        Check(layout.Build(Utf8("<size=500><size=2000>A</size>A</size>"), faces, options) == LayoutError::None
                && Near(layout.GetGlyphs()[0].size, 2000.0f) && Near(layout.GetGlyphs()[1].size, 500.0f), "the inner size wins");

        // 줄 높이: 둘째 줄에 2000 px 글자가 있으면 그 줄의 높이와 올림이 두 배다.
        // 큰 글자가 줄의 첫 글자가 아니어도 그 줄의 높이다.
        Check(layout.Build(Utf8("A\nA<size=2000>A</size>"), faces, options) == LayoutError::None && layout.GetLines().Size() == 2,
            "two lines of different sizes lay out");
        Check(Near(layout.GetLines()[0].height, 1448.0f) && Near(layout.GetLines()[1].height, 2896.0f), "each line is as tall as its largest letter");
        Check(Near(layout.GetLines()[0].baseline, 0.0f) && Near(layout.GetLines()[1].baseline, 1160.0f - 1448.0f - 2320.0f),
            "the second baseline drops by the first line and its own ascent");
        Check(Near(layout.GetContentHeight(), 1448.0f + 2896.0f), "the content height adds the lines");
        Check(Near(layout.GetGlyphs()[2].y, layout.GetLines()[1].baseline) && Near(layout.GetGlyphs()[2].size, 2000.0f),
            "the large A sits on its line");

        // 배율(자동 크기가 쓴다)과 정수 반올림(비트맵이 쓴다).
        LayoutOptions scaled = options;
        scaled.markupScale = 0.5f;
        Check(layout.Build(Utf8("<size=2000>A</size>"), faces, scaled) == LayoutError::None && Near(layout.GetGlyphs()[0].size, 1000.0f),
            "markupScale scales tag sizes");
        LayoutOptions whole = options;
        whole.wholePixelMarkup = true;
        Check(layout.Build(Utf8("<size=10.4>A</size>"), faces, whole) == LayoutError::None && Near(layout.GetGlyphs()[0].size, 10.0f),
            "wholePixelMarkup rounds tag sizes");

        // 자동 크기는 태그 크기도 같은 비로 줄인다: 상자 폭 608 에 `A<size=2000>A</size>` 는 608 x 3 = 1824 폭이라 1/3 로 준다.
        LayoutOptions fit = options;
        fit.boxWidth = 608.0f;
        float chosen = 0.0f;
        Check(layout.BuildToFit(Utf8("A<size=2000>A</size>"), faces, fit, 10.0f, 1000.0f, 0.0f, chosen) == LayoutError::None,
            "a sized text fits its box");
        Check(chosen < 340.0f && chosen > 320.0f && Near(layout.GetGlyphs()[1].size, chosen * 2.0f),
            "auto size shrinks the tagged letter with the rest");
    }

    // **`<b>`·`<i>` 와 스타일 face**(D-225). 굵게 face 를 둘째 face 로 주면 `<b>` 안의 글자는 그 face 에서 온다. 기울임 face 가 없으면
    // 기울임 글자는 보통 face 다. 굵은 기울임은 굵은 기울임 → 굵게 순으로 찾는다. 스타일 face 에 없는 글자(한글)는 폴백 순서로 간다.
    void TestBoldAndItalicTagsPickStyleFaces()
    {
        const FontFace regular = LoadTestFont();
        FontFace bold;
        Check(bold.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKRLatin),
                  sizeof(TestFontNotoSansKRLatin))),
            "the latin face loads as a stand-in bold");
        const FontFace* faces[] = { &regular, &bold };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.richText = true;
        options.boldFace = 1;
        Check(layout.Build(Utf8("A<b>A</b><i>A</i><b><i>A</i></b>"), faces, options) == LayoutError::None
                && layout.GetGlyphs().Size() == 4, "styled letters lay out");
        const ArrayView<const PositionedGlyph> glyphs = layout.GetGlyphs();
        Check(glyphs[0].face == 0 && glyphs[0].style == GlyphStyleRegular, "a plain letter is regular");
        Check(glyphs[1].face == 1 && glyphs[1].style == GlyphStyleBold, "a bold letter comes from the bold face");
        Check(glyphs[2].face == 0 && glyphs[2].style == GlyphStyleItalic, "without an italic face an italic letter stays regular");
        Check(glyphs[3].face == 1 && glyphs[3].style == (GlyphStyleBold | GlyphStyleItalic),
            "bold italic falls back to the bold face");
        options.boldItalicFace = 0;
        Check(layout.Build(Utf8("<b><i>A</i></b>"), faces, options) == LayoutError::None && layout.GetGlyphs()[0].face == 0,
            "a bold italic face is taken first when there is one");
        options.boldItalicFace = LayoutOptions::NoStyleFace;
        // 굵게 face(라틴)에 한글이 없다: 폴백 순서라 첫 face 의 한글이다.
        Check(layout.Build(Utf8("<b>\xED\x95\x9C</b>"), faces, options) == LayoutError::None && layout.GetGlyphs()[0].face == 0
                && layout.GetGlyphs()[0].glyph == regular.FindGlyph(U'\uD55C'),
            "a letter the bold face lacks falls back in order");
        Check(layout.Build(Utf8("</b>A"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 5,
            "an unmatched </b> is text");
        options.richText = false;
        Check(layout.Build(Utf8("<b>A</b>"), faces, options) == LayoutError::None && layout.GetGlyphs().Size() == 8,
            "without richText the tags are text");
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

    // **탭 멈춤 자리와 금칙**(text-plan §7 의 1 단계 남은 일). 탭은 줄 머리에서 센 다음 멈춤 자리(공백 폭 x tabSize)까지 나아가고,
    // 닫는 괄호로 줄을 시작하거나 여는 괄호로 줄을 끝내지 않는다 - 끊을 다른 자리가 있으면 그리로 옮긴다.
    // **자동 크기**(text-plan §4.2). 줄 높이는 em 의 1.448 배, `hello` 2.335·`world` 2.696·`hello world` 5.255 em 이다.
    void TestBuildToFitFindsTheLargestSize()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        options.overflow = Overflow::Wrap;
        float chosen = 0.0f;

        // 한 줄 높이의 상자: 폭이 정한다(5.255 s ≤ 5255 → 1000).
        options.boxWidth = 5255.0f;
        options.boxHeight = 1448.0f;
        Check(layout.BuildToFit(Utf8("hello world"), faces, options, 10.0f, 4000.0f, 1.0f, chosen) == LayoutError::None,
            "a one-line box fits");
        Check(Near(chosen, 1000.0f) && LineCounts(layout, { 10 }), "the one-line box takes the size where the line just fits");
        // 한 줄 반 높이: 두 줄로 나뉘어 높이가 정한다(2.896 s ≤ 4344 → 1500).
        options.boxHeight = 4344.0f;
        Check(layout.BuildToFit(Utf8("hello world"), faces, options, 10.0f, 4000.0f, 1.0f, chosen) == LayoutError::None,
            "a taller box fits");
        Check(Near(chosen, 1500.0f) && LineCounts(layout, { 5, 5 }), "the taller box wraps and the height decides");
        // `Word` 는 어절을 글자에서 끊지 않는 크기를 고른다(2.335 s ≤ 2000 → 856).
        options.boxWidth = 2000.0f;
        options.boxHeight = 100000.0f;
        Check(layout.BuildToFit(Utf8("hello"), faces, options, 10.0f, 4000.0f, 1.0f, chosen) == LayoutError::None,
            "a narrow box fits");
        Check(Near(chosen, 856.0f) && layout.GetForcedBreakCount() == 0, "a word is not broken to make it fit");
        // 가장 작은 크기로도 넘치면 그 크기다.
        Check(layout.BuildToFit(Utf8("hello"), faces, options, 3000.0f, 4000.0f, 1.0f, chosen) == LayoutError::None
                && Near(chosen, 3000.0f),
            "past the smallest size the text overflows at that size");
        // step 0 은 0.25 칸까지 좁힌다(SDF).
        options.boxWidth = 2001.0f;
        Check(layout.BuildToFit(Utf8("hello"), faces, options, 10.0f, 4000.0f, 0.0f, chosen) == LayoutError::None
                && chosen > 856.0f && chosen <= 857.0f && std::fmod(chosen, 0.25f) == 0.0f,
            "a continuous fit lands on a quarter pixel");
        // 다시 맞춰도 할당하지 않는다.
        const std::size_t capacity = layout.GetReservedCapacity();
        Check(layout.BuildToFit(Utf8("hello"), faces, options, 10.0f, 4000.0f, 0.0f, chosen) == LayoutError::None
                && layout.GetReservedCapacity() == capacity,
            "fitting again reuses the storage");
    }

    void TestTabStopsAndLineBreakRules()
    {
        const FontFace face = LoadTestFont();
        const FontFace* faces[] = { &face };
        TextLayout layout;
        LayoutOptions options = Unscaled();
        // 공백 224 라 멈춤 간격은 896 이다. A = 608.
        Check(layout.Build(Utf8("A\tB"), faces, options) == LayoutError::None, "a tab lays out");
        Check(layout.GetGlyphs().Size() == 2 && Near(layout.GetGlyphs()[1].x, 896.0f), "a tab after A goes to the first stop");
        Check(layout.Build(Utf8("\t\tB"), faces, options) == LayoutError::None, "two tabs lay out");
        Check(Near(layout.GetGlyphs()[0].x, 1792.0f), "two tabs from the line head reach the second stop");
        Check(layout.Build(Utf8("AAAA\tB"), faces, options) == LayoutError::None, "a tab past a stop lays out");
        Check(Near(layout.GetGlyphs()[4].x, 2688.0f), "four As (2432) tab to the third stop");
        Check(layout.Build(Utf8("A\nA\tB"), faces, options) == LayoutError::None, "a tab on a second line lays out");
        Check(Near(layout.GetGlyphs()[2].x, 896.0f), "stops count from the head of each line");
        options.tabSize = 0.0f;
        Check(layout.Build(Utf8("A\tB"), faces, options) == LayoutError::None, "a zero tab size lays out");
        Check(Near(layout.GetGlyphs()[1].x, 608.0f + 224.0f), "with no stops a tab is one space");

        // 금칙: 글자 셋이 들어가는 상자에서 넷째가 넘친다.
        options = Unscaled();
        options.overflow = Overflow::Wrap;
        options.wrapMode = WrapMode::Character;
        options.boxWidth = 608.0f * 3.0f + 10.0f;
        Check(layout.Build(Utf8("AAA)"), faces, options) == LayoutError::None, "a closing bracket lays out");
        Check(LineCounts(layout, { 2, 2 }), "a closing bracket does not start a line - the A before it goes down with it");
        Check(layout.Build(Utf8("AA(A"), faces, options) == LayoutError::None, "an opening bracket lays out");
        Check(LineCounts(layout, { 2, 2 }), "an opening bracket does not end a line - it goes down to what it opens");
        Check(layout.Build(Utf8("AAAA"), faces, options) == LayoutError::None, "plain letters lay out");
        Check(LineCounts(layout, { 3, 1 }), "without the rule the break stays after the third letter");
        // 끊을 다른 자리가 없으면 금칙을 어기고라도 끊는다(멈추지 않는다).
        options.boxWidth = 700.0f;
        Check(layout.Build(Utf8("A)"), faces, options) == LayoutError::None, "a box for one letter lays out");
        Check(LineCounts(layout, { 1, 1 }), "with no other place the closing bracket still wraps");
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
        TestKerningSurvivesGposShapesStbSkipped();
        TestCombiningMarksAttachToTheirBase();
        TestOldHangulJoinsThroughGsub();
        TestRichTextMarkup();
        TestBoldAndItalicTagsPickStyleFaces();
        TestWordWrap();
        TestHangulWrapModes();
        TestTabStopsAndLineBreakRules();
        TestBuildToFitFindsTheLargestSize();
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
