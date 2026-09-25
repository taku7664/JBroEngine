#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstddef>
#include <cstdint>

// 폰트 face 하나다(D-200, text-plan §4.1). TTF·OTF(CFF) 바이트를 복사해 들고 stb_truetype 으로 읽는다.
//
// stb 의 타입은 이 헤더에 나오지 않는다 - 작은 글자 품질이나 복잡한 문자 체계 때문에 FreeType 으로 바꾸게 되어도
// 이 표면과 그 위의 레이아웃은 그대로다(text-plan §3.4). 모든 치수는 **폰트 단위**(unitsPerEm 기준, y 위쪽이 양수)다.
// 픽셀로 옮기는 것은 레이아웃이 한다.
//
// 폰트는 프로젝트 에셋이라 믿는 입력으로 본다. stb 는 망가진 표를 끝까지 검사하지 않는다.
namespace JBro::Text
{
    using GlyphIndex = std::uint32_t;

    // 폰트에 없는 글자다. 모든 폰트의 0 번 글리프는 .notdef 다.
    inline constexpr GlyphIndex MissingGlyph = 0;

    // 가로쓰기 세로 치수(hhea). ascent 는 양수, descent 는 음수다.
    struct FontMetrics
    {
        std::int32_t unitsPerEm = 0;
        std::int32_t ascent = 0;
        std::int32_t descent = 0;
        std::int32_t lineGap = 0;
    };

    // 글리프 외곽선의 상자다. 공백처럼 그릴 것이 없으면 empty 이고 나머지는 0 이다.
    struct GlyphBox
    {
        std::int32_t minX = 0;
        std::int32_t minY = 0;
        std::int32_t maxX = 0;
        std::int32_t maxY = 0;
        bool         empty = true;
    };

    class FontFace final
    {
    public:
        FontFace() = default;
        ~FontFace() = default;

        // stb 의 상태는 바이트 버퍼를 가리키는 포인터를 든다. 옮기면 버퍼도 함께 옮겨 가므로(Array 의 이동은 버퍼를 넘긴다)
        // 포인터가 그대로 맞다. 복사는 그 포인터가 원본을 가리키게 되므로 막는다.
        FontFace(FontFace&& other) noexcept;
        FontFace& operator=(FontFace&& other) noexcept;
        FontFace(const FontFace&) = delete;
        FontFace& operator=(const FontFace&) = delete;

        // 바이트를 복사해 face 를 연다. faceIndex 는 TTC 안의 번호다. 실패하면 거짓이고 아무것도 들지 않는다.
        bool Load(ArrayView<const std::byte> bytes, std::uint32_t faceIndex = 0);
        void Unload();
        bool IsLoaded() const;

        // 코드포인트의 글리프다. 없으면 MissingGlyph.
        GlyphIndex FindGlyph(char32_t codepoint) const;

        const FontMetrics& GetMetrics() const;
        std::int32_t GetAdvance(GlyphIndex glyph) const;
        // 두 글리프 사이의 커닝이다. GPOS 쌍 조정을 먼저, 없으면 kern 표를 본다. 대개 음수다.
        std::int32_t GetKerning(GlyphIndex left, GlyphIndex right) const;
        GlyphBox GetGlyphBox(GlyphIndex glyph) const;

    private:
        // stbtt_fontinfo 를 담는 자리다. 크기는 FontFace.cpp 가 단언한다. 헤더에 stb 를 들이지 않으려고 불투명하게 둔다.
        static constexpr std::size_t InfoStorageSize = 192;

        void MoveFrom(FontFace& other) noexcept;

        Array<std::byte> m_bytes;
        FontMetrics      m_metrics;
        bool             m_loaded = false;
        alignas(8) unsigned char m_info[InfoStorageSize] = {};
    };
}
