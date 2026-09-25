#pragma once

#include <JBro/Text/FontFace.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>

// face 하나의 글리프 비트맵을 담는 CPU 아틀라스다(D-200, text-plan §3.6·§4.4).
//
// 페이지는 정사각형 RGBA8 이고 픽셀은 `(255, 255, 255, 커버리지)` 다 - 지금 스프라이트 셰이더(`texture * tint`)가 그대로
// 글자 색으로 그린다(§3.5, 렌더러 변경 없음). 칸은 선반(shelf) 할당이고 한 번 준 칸은 옮기지 않는다: 페이지가 차면
// 새 페이지를 연다. 그래서 이미 나간 UV 가 바뀌지 않는다.
//
// GPU 는 모른다. 새 글리프가 들어간 페이지에 "더러움" 표시를 하고, 올리는 쪽(Framework2DSystem)이 프레임 밖에서
// 그 페이지만 올린 뒤 표시를 지운다. 새 글리프가 없는 프레임에는 올릴 것이 없다.
namespace JBro::Text
{
    // 아틀라스 안의 글리프 한 칸이다. 좌표는 페이지 픽셀(왼쪽 위 원점)이다.
    // left·top 은 기준선 위 원점에서 비트맵 왼쪽 위 모서리까지의 거리(픽셀, top 은 위쪽이 양수)다.
    struct AtlasGlyph
    {
        std::uint16_t page = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::int16_t  left = 0;
        std::int16_t  top = 0;
        bool          empty = true; // 공백처럼 그릴 것이 없다. 칸이 없다
    };

    enum class AtlasError : std::uint8_t
    {
        None,
        FaceNotLoaded,
        InvalidPixelSize, // 1 ~ MaxPixelSize 밖
        GlyphTooLarge,    // 한 페이지보다 크다
    };

    class GlyphAtlas final
    {
    public:
        static constexpr std::uint32_t DefaultPageSize = 1024;
        static constexpr std::uint32_t MaxPixelSize = 512;

        explicit GlyphAtlas(std::uint32_t pageSize = DefaultPageSize);

        // (pixelSize, glyph) 의 칸을 준다. 처음이면 face 로 래스터화해 칸을 잡고 그 페이지를 더럽힌다.
        // pixelSize 는 em 픽셀이다(TextLayout 의 fontSize 와 같은 뜻). face 는 이 아틀라스의 주인 하나만 넘긴다.
        AtlasError Ensure(const FontFace& face, std::uint32_t pixelSize, GlyphIndex glyph, AtlasGlyph& out);

        // 이미 있는 칸만 찾는다. 래스터화하지 않는다.
        const AtlasGlyph* Find(std::uint32_t pixelSize, GlyphIndex glyph) const;

        std::uint32_t GetPageSize() const;
        std::uint32_t GetPageCount() const;
        // 한 페이지의 픽셀이다(RGBA8, 행마다 pageSize * 4 바이트, 위에서 아래로).
        ArrayView<const std::byte> GetPagePixels(std::uint32_t page) const;
        bool IsPageDirty(std::uint32_t page) const;
        void ClearPageDirty(std::uint32_t page);

        std::uint32_t GetGlyphCount() const;
        // 모든 칸과 페이지를 버린다. 폰트가 다시 로드되면 부른다(옛 글리프는 옛 바이트의 것이다).
        void Clear();

    private:
        struct Page
        {
            Array<std::byte> pixels;
            std::uint32_t    cursorX = 1;
            std::uint32_t    cursorY = 1;
            std::uint32_t    shelfHeight = 0;
            bool             dirty = false;
        };

        static std::uint64_t Key(std::uint32_t pixelSize, GlyphIndex glyph);
        bool Allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& page, std::uint32_t& x, std::uint32_t& y);

        std::uint32_t                   m_pageSize = DefaultPageSize;
        Array<Page>                     m_pages;
        Table<std::uint64_t, AtlasGlyph> m_glyphs;
        Array<std::uint8_t>             m_scratch; // 래스터화 버퍼(커버리지 한 채널). 용량을 다시 쓴다
    };
}
