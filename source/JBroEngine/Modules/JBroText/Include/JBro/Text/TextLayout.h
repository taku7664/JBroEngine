#pragma once

#include <JBro/Text/FontFace.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstdint>

// UTF-8 한 덩어리를 줄로 나누고 글리프마다 자리를 매긴다(D-200, text-plan §3.8·§4.1).
//
// 캔버스·컴포넌트·렌더러를 모른다. 입력은 바이트와 face 목록과 옵션이고, 출력은 POD 배열이다.
// 좌표는 **픽셀**이고 y 가 위쪽이며, 원점은 정렬이 정하는 기준점이다(아래 AlignX·AlignY).
//
// 같은 TextLayout 을 다시 쓰면 안쪽 배열의 용량이 남으므로, 전보다 길지 않은 입력에서는 할당하지 않는다.
// 글자가 바뀐 텍스트를 매 프레임 다시 레이아웃해도 힙을 타지 않게 하려는 것이다(text-plan §1.2 의 3 번).
namespace JBro::Text
{
    // 상자를 넘는 줄을 어떻게 하나. Wrap 과 Clip 은 boxWidth 에서 줄을 바꾸고, Clip 은 boxHeight 밖의 줄을 버린다.
    // Overflow 는 명시적 개행에서만 줄을 바꾼다.
    enum class Overflow : std::uint8_t
    {
        Overflow,
        Wrap,
        Clip,
    };

    // 어디서 줄을 바꿀 수 있나. Word 는 공백 뒤에서만 바꾸고 한글도 어절을 지킨다. 한자·가나는 글자 사이에서도 바꾼다
    // (띄어 쓰지 않는 문자 체계다). Character 는 글자 사이 어디서나 바꾼다. 두 경우 모두, 줄에 기회가 없는 긴 단어는
    // 글자에서 끊는다.
    enum class WrapMode : std::uint8_t
    {
        Word,
        Character,
    };

    // 블록(가로는 boxWidth 가 있으면 그것, 없으면 가장 긴 줄)의 어느 가로 자리가 x = 0 인가. 줄은 블록 안에서 같은 쪽으로 붙는다.
    enum class AlignX : std::uint8_t
    {
        Left,
        Center,
        Right,
    };

    // 블록(세로는 boxHeight 가 있으면 그것, 없으면 줄 수 x 줄 높이)의 어느 세로 자리가 y = 0 인가.
    // Baseline 은 첫 줄의 기준선이다.
    enum class AlignY : std::uint8_t
    {
        Top,
        Middle,
        Baseline,
        Bottom,
    };

    struct LayoutOptions
    {
        float    fontSize = 32.0f;     // em 크기(픽셀)
        float    boxWidth = 0.0f;      // 0 이면 제한 없음
        float    boxHeight = 0.0f;     // 0 이면 제한 없음
        Overflow overflow = Overflow::Wrap;
        WrapMode wrapMode = WrapMode::Word;
        AlignX   alignX = AlignX::Left;
        AlignY   alignY = AlignY::Baseline;
        float    lineSpacing = 1.0f;   // 줄 높이 배율
        float    letterSpacing = 0.0f; // 글자 사이에 더하는 픽셀
    };

    // 그릴 글리프 하나다. 공백과 개행은 들어오지 않는다. (x, y) 는 기준선 위의 글리프 원점이다.
    struct PositionedGlyph
    {
        float         x = 0.0f;
        float         y = 0.0f;
        GlyphIndex    glyph = MissingGlyph;
        std::uint16_t face = 0;         // faces 안의 번호
        std::uint16_t line = 0;
        std::uint32_t sourceOffset = 0; // 이 글자가 시작하는 UTF-8 바이트 위치
    };

    struct LineInfo
    {
        std::uint32_t firstGlyph = 0;
        std::uint32_t glyphCount = 0;
        float         width = 0.0f;    // 줄 끝 공백을 뺀 폭
        float         baseline = 0.0f; // 기준선의 y
        std::uint32_t sourceBegin = 0;
        std::uint32_t sourceEnd = 0;
    };

    enum class LayoutError : std::uint8_t
    {
        None,
        NoFace,          // 열린 face 가 하나도 없다
        InvalidFontSize, // 0 이하이거나 수가 아니다
        TooLong,         // 글자·줄 번호가 담을 수 있는 범위를 넘는다
    };

    class TextLayout final
    {
    public:
        // faces 의 앞이 기본 폰트이고 뒤는 폴백이다. 글자마다 앞에서부터 그 글자가 있는 face 를 고른다.
        // 줄 높이는 첫 번째 열린 face 의 치수로 정한다. 실패하면 결과를 비운다.
        LayoutError Build(ArrayView<const char> utf8, ArrayView<const FontFace* const> faces, const LayoutOptions& options);

        ArrayView<const PositionedGlyph> GetGlyphs() const;
        ArrayView<const LineInfo> GetLines() const;

        // 블록 사각형이다(정렬 기준점 기준 픽셀). 에디터의 선택과 컬링이 쓴다.
        float GetMinX() const;
        float GetMinY() const;
        float GetMaxX() const;
        float GetMaxY() const;

    private:
        enum class ItemKind : std::uint8_t
        {
            Visible,
            Space,
            Newline,
        };

        struct Codepoint
        {
            char32_t      value = 0;
            std::uint32_t offset = 0;
        };

        struct Item
        {
            char32_t      codepoint = 0;
            GlyphIndex    glyph = MissingGlyph;
            std::uint16_t face = 0;
            ItemKind      kind = ItemKind::Visible;
            bool          breaksAnywhere = false; // 이 글자의 앞뒤가 늘 줄바꿈 기회다(한자·가나, Character 모드의 전부)
            std::uint32_t offset = 0;
            float         advance = 0.0f;         // 픽셀, 커닝 전
            float         x = 0.0f;               // 줄 안에서 매긴 자리
        };

        void Reset();

        Array<Codepoint>       m_codepoints;
        Array<Item>            m_items;
        Array<PositionedGlyph> m_glyphs;
        Array<LineInfo>        m_lines;
        float                  m_minX = 0.0f;
        float                  m_minY = 0.0f;
        float                  m_maxX = 0.0f;
        float                  m_maxY = 0.0f;
    };
}
