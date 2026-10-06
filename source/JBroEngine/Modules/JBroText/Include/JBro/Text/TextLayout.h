#pragma once

#include <JBro/Text/FontFace.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

// UTF-8 한 덩어리를 줄로 나누고 글리프마다 자리를 매긴다(D-200, text-plan §3.8·§4.1).
//
// 캔버스·컴포넌트·렌더러를 모른다. 입력은 바이트와 face 목록과 옵션이고, 출력은 POD 배열이다.
// 좌표는 **픽셀**이고 y 가 위쪽이며, 원점은 정렬이 정하는 기준점이다(아래 AlignX·AlignY).
//
// 같은 TextLayout 을 다시 쓰면 안쪽 배열의 용량이 남으므로, 전보다 길지 않은 입력에서는 할당하지 않는다.
// 글자가 바뀐 텍스트를 매 프레임 다시 레이아웃해도 힙을 타지 않게 하려는 것이다(text-plan §1.2 의 3 번).
namespace JBro::Text
{
    // 상자를 넘는 줄을 어떻게 하나. Wrap 과 Clip 은 boxWidth 에서 줄을 바꾸고, Clip 은 위쪽이 boxHeight 밖에 있는 줄을 버린다
    // (걸친 줄은 남는다 - 글리프를 상자에 맞게 자르는 것은 그리는 쪽이다).
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
        Float    fontSize = 32.0f;     // em 크기(픽셀)
        Float    boxWidth = 0.0f;      // 0 이면 제한 없음
        Float    boxHeight = 0.0f;     // 0 이면 제한 없음
        Overflow overflow = Overflow::Wrap;
        WrapMode wrapMode = WrapMode::Word;
        AlignX   alignX = AlignX::Left;
        AlignY   alignY = AlignY::Baseline;
        Float    lineSpacing = 1.0f;   // 줄 높이 배율
        Float    letterSpacing = 0.0f; // 글자 사이에 더하는 픽셀
        // 탭 멈춤 자리의 간격이다(기본 폰트의 공백 폭 몇 개인가). 탭은 줄 머리에서 센 다음 멈춤 자리까지 나아간다. 0 이하면 공백 하나다.
        Float    tabSize = 4.0f;
        // **리치 텍스트**(D-221). 켜면 `<color=#RRGGBB>`·`<color=#RRGGBBAA>` ... `</color>` 와 `<size=픽셀>` ... `</size>` 를 태그로 읽고
        // 글자로 내지 않는다. `<<` 는 `<` 한 글자다. 모르는 태그·틀린 태그·짝 없는 닫는 태그·여덟 겹을 넘는 태그는 글자 그대로 보인다.
        Bool     richText = false;
        // `<size>` 에 곱하는 배율이다. 자동 크기(BuildToFit)가 고른 크기 / fontSize 로 둔다.
        Float    markupScale = 1.0f;
        // `<size>` 를 정수 픽셀로 반올림한다. 비트맵 폰트는 정수 크기마다 뜨므로 레이아웃도 그 크기로 재야 한다.
        Bool     wholePixelMarkup = false;
        // **스타일 face**(D-225). 리치 텍스트의 `<b>`·`<i>` 가 붙은 글자가 먼저 볼 faces 안의 번호다(굵게·기울임·굵은 기울임 순).
        // `NoStyleFace` 면 그 스타일의 face 가 없다 - 굵은 기울임은 굵게, 기울임 순으로, 모두 없으면 보통 글자와 같은 face 를 쓴다.
        // 그 face 에 글자가 없으면 폴백 순서(faces 앞에서부터)로 간다.
        static constexpr std::uint16_t NoStyleFace = 0xFFFF;
        std::uint16_t boldFace = NoStyleFace;
        std::uint16_t italicFace = NoStyleFace;
        std::uint16_t boldItalicFace = NoStyleFace;
    };

    // 글자의 스타일 비트다(리치 텍스트의 `<b>`·`<i>`).
    enum GlyphStyle : std::uint8_t
    {
        GlyphStyleRegular = 0,
        GlyphStyleBold = 1,
        GlyphStyleItalic = 2,
    };

    // 그릴 글리프 하나다. 공백과 개행은 들어오지 않는다. (x, y) 는 기준선 위의 글리프 원점이다.
    // 결합 표시(U+0300 따위)는 앞 글자에 붙어 제 글리프로 들어온다 - 폰트의 GPOS mark-to-base 앵커가 있으면 그 자리, 없으면 받침의
    // 전진 폭 끝(폭 없는 표시가 음수 베어링으로 받침 위에 그려지는 폰트의 기본 자리)이다. 표시 위의 표시(mark-to-mark)는 읽지 않아
    // 같은 받침의 두 표시는 같은 앵커에 겹친다.
    struct PositionedGlyph
    {
        Float         x = 0.0f;
        Float         y = 0.0f;
        GlyphIndex    glyph = MissingGlyph;
        std::uint16_t face = 0;         // faces 안의 번호
        std::uint16_t line = 0;
        UInt32 sourceOffset = 0; // 이 글자가 시작하는 UTF-8 바이트 위치
        Float         size = 0.0f;      // 이 글자의 em 크기(픽셀). 리치 텍스트의 `<size>` 밖이면 fontSize 다
        UInt32 color = 0;        // hasColor 이면 `<color>` 의 RGBA8(R 이 가장 낮은 바이트)
        Bool          hasColor = false;
        std::uint8_t  style = GlyphStyleRegular; // `GlyphStyle` 비트
    };

    struct LineInfo
    {
        UInt32 firstGlyph = 0;
        UInt32 glyphCount = 0;
        Float         width = 0.0f;    // 줄 끝 공백을 뺀 폭
        Float         baseline = 0.0f; // 기준선의 y
        Float         size = 0.0f;     // 줄에서 가장 큰 글자의 em 크기(픽셀). 줄 높이와 기준선은 이 크기로 잰다
        Float         height = 0.0f;   // 줄 높이(픽셀, 줄 간격 배율을 곱한 것)
        UInt32 sourceBegin = 0;
        UInt32 sourceEnd = 0;
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

        // **자동 크기**(text-plan §4.2). [minSize, maxSize] 에서 상자에 들어가는 가장 큰 글자 크기를 찾아 그 크기로 레이아웃해 둔다.
        // "들어간다" 는 가장 긴 줄이 상자 폭 안이고(폭이 있으면), 줄을 모두 쌓은 높이가 상자 높이 안이며(높이가 있으면), `Word` 에서
        // 어절을 글자에서 끊지 않았다는 뜻이다. step 이 1 이면 정수 크기만(비트맵), 0 이면 0.25 픽셀까지 좁힌다(SDF). 가장 작은 크기로도
        // 넘치면 그 크기다. 이진 탐색이라 Build 를 크기 범위의 로그만큼 부르고, 다시 부를 때 안쪽 배열의 용량을 그대로 쓴다.
        LayoutError BuildToFit(ArrayView<const char> utf8, ArrayView<const FontFace* const> faces, const LayoutOptions& options,
            Float minSize, Float maxSize, Float step, Float& chosenSize);

        // 줄을 나눈 뒤의 내용 크기다(자르기 전). 가장 긴 줄의 폭, 줄 수 x 줄 높이.
        Float GetContentWidth() const;
        Float GetContentHeight() const;
        // 끊을 자리가 없어 넘친 글자에서 억지로 끊은 횟수다. `Word` 에서 0 이 아니면 어절이 글자에서 갈렸다.
        UInt32 GetForcedBreakCount() const;

        ArrayView<const PositionedGlyph> GetGlyphs() const;
        ArrayView<const LineInfo> GetLines() const;

        // 블록 사각형이다(정렬 기준점 기준 픽셀). 에디터의 선택과 컬링이 쓴다.
        Float GetMinX() const;
        Float GetMinY() const;
        Float GetMaxX() const;
        Float GetMaxY() const;

        // 안쪽 배열 넷(코드포인트·글자·글리프·줄)이 잡아 둔 원소 수의 합이다. 다시 레이아웃해도 이 값이 그대로면
        // 용량을 다시 썼다는 뜻이다 - 주소 비교는 풀었다 다시 잡은 블록이 같은 주소로 올 수 있어 그 증거가 못 된다.
        std::size_t GetReservedCapacity() const;

    private:
        enum class ItemKind : std::uint8_t
        {
            Visible,
            Mark,    // 앞 글자에 붙는 결합 표시다. 폭이 없고, 줄바꿈 기회가 아니며, 넘침을 재지 않는다
            Space,
            Newline,
        };

        struct Codepoint
        {
            char32_t      value = 0;
            UInt32 offset = 0;
            Float         size = 0.0f;
            UInt32 color = 0;
            Bool          hasColor = false;
            std::uint8_t  style = 0;
        };

        struct Item
        {
            char32_t      codepoint = 0;
            GlyphIndex    glyph = MissingGlyph;
            std::uint16_t face = 0;
            ItemKind      kind = ItemKind::Visible;
            Bool          breaksAnywhere = false; // 이 글자의 앞뒤가 늘 줄바꿈 기회다(한자·가나, Character 모드의 전부)
            UInt32 offset = 0;
            Float         advance = 0.0f;         // 픽셀, 커닝 전
            Float         x = 0.0f;               // 줄 안에서 매긴 자리
            UInt32 markBase = 0;           // Mark 이면 붙는 받침 글자의 번호
            Float         markX = 0.0f;           // Mark 이면 받침 원점에서 표시 원점까지(픽셀, y 위쪽)
            Float         markY = 0.0f;
            Float         size = 0.0f;            // em 크기(픽셀)
            UInt32 color = 0;
            Bool          hasColor = false;
            std::uint8_t  style = 0;
        };

        void Reset();

        Array<Codepoint>       m_codepoints;
        Array<Item>            m_items;
        Array<PositionedGlyph> m_glyphs;
        Array<LineInfo>        m_lines;
        Float                  m_contentWidth = 0.0f;
        Float                  m_contentHeight = 0.0f;
        UInt32          m_forcedBreaks = 0;
        Float                  m_minX = 0.0f;
        Float                  m_minY = 0.0f;
        Float                  m_maxX = 0.0f;
        Float                  m_maxY = 0.0f;
    };
}
