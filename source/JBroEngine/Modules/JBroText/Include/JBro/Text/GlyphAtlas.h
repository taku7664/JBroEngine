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

    // 미리 뜰 글자 벌이다(text-plan §3.6). `Ksx1001` 은 ASCII 와 KS X 1001 의 한글 2,350 자다 - 대부분의 한국어 게임 글이
    // 런타임 래스터화 없이 그려진다.
    enum class PrewarmSet : std::uint8_t
    {
        None,
        Ascii,
        Ksx1001,
    };

    // 미리 뜬 아틀라스의 표지다(D-232). 폰트 원본과 미리 뜨기 설정이 같을 때만 되살린다 - 폰트를 바꾸거나 설정을 고친 뒤의 옛 아틀라스를
    // 쓰지 않는다.
    struct BakedAtlasStamp
    {
        std::uint64_t sourceHash = 0; // 폰트 원본 바이트의 64 비트 FNV-1a(`HashFontSource`)
        PrewarmSet    set = PrewarmSet::None;
        std::uint32_t pixelSize = 0;
        std::uint32_t sdfSpread = 0;  // 0 이면 비트맵이다
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
        // 같은 칸을 SDF 로 준다(`FontFace::RasterizeGlyphSdf`, 4 단계). 픽셀은 `(255, 255, 255, 거리)` 다. 칸의 left·top·크기는
        // 퍼짐까지 포함한 거리장 상자다. 비트맵 칸과 키가 갈라 한 아틀라스에 섞여도 서로 덮지 않는다.
        AtlasError EnsureSdf(const FontFace& face, std::uint32_t pixelSize, std::uint32_t spread, GlyphIndex glyph, AtlasGlyph& out);
        static constexpr std::uint32_t MaxSdfSpread = 64;

        // 한 벌의 글자를 한 번에 뜬다. sdfSpread 가 0 이면 pixelSize 의 비트맵, 아니면 그 크기·퍼짐의 거리장이다. 폰트에 없는 글자는
        // 건너뛴다. 넣은 칸 수(이미 있던 것은 세지 않는다)를 준다. 폰트를 열 때 한 번 부른다 - 글자마다 래스터화하므로 프레임 안에서
        // 부르지 않는다.
        std::uint32_t Prewarm(const FontFace& face, PrewarmSet set, std::uint32_t pixelSize, std::uint32_t sdfSpread);

        // **워커에서 뜬 칸을 넣는다**(비동기 미리 뜨기). box·alpha 는 `FontFace::RasterizeGlyph`·`RasterizeGlyphSdf` 가 준 것이다.
        // 그 사이 같은 칸이 이미 섰으면(레이아웃이 먼저 뜬 글자) 넣지 않고 거짓이다. 빈 글리프는 box 가 0 이다. 메인 스레드에서 부른다.
        bool Insert(std::uint32_t pixelSize, std::uint32_t sdfSpread, GlyphIndex glyph, const GlyphBitmapBox& box,
            const std::uint8_t* alpha);
        // 벌에 든 글자 가운데 폰트에 있는 것의 글리프 번호를 모은다(중복 없음, 코드포인트 순서).
        static void CollectPrewarmGlyphs(const FontFace& face, PrewarmSet set, Array<GlyphIndex>& glyphs);

        // 이미 있는 칸만 찾는다. 래스터화하지 않는다.
        const AtlasGlyph* Find(std::uint32_t pixelSize, GlyphIndex glyph) const;

        std::uint32_t GetPageSize() const;
        std::uint32_t GetPageCount() const;
        // 한 페이지의 픽셀이다(RGBA8, 행마다 pageSize * 4 바이트, 위에서 아래로).
        ArrayView<const std::byte> GetPagePixels(std::uint32_t page) const;
        bool IsPageDirty(std::uint32_t page) const;
        // 더러운 페이지에서 새 칸들을 감싸는 사각형이다(픽셀). 올리는 쪽은 이 사각형만 올린다. 깨끗하면 크기가 0 이다.
        void GetPageDirtyRect(std::uint32_t page, std::uint32_t& x, std::uint32_t& y, std::uint32_t& width, std::uint32_t& height) const;
        void ClearPageDirty(std::uint32_t page);

        std::uint32_t GetGlyphCount() const;
        // 모든 칸과 페이지를 버린다. 폰트가 다시 로드되면 부른다(옛 글리프는 옛 바이트의 것이다).
        void Clear();

        // **미리 뜬 아틀라스를 싸고 되살린다**(D-232, package-plan §2.5). 페이지(커버리지 한 채널)·칸 표·페이지마다의 선반 자리를 적는다 -
        // 되살린 뒤 새 글자가 같은 페이지를 이어 채워도 겹치지 않는다. 게임 빌드가 싸고, 게임의 텍스트 라이브러리가 폰트를 열 때 되살린다.
        void Bake(const BakedAtlasStamp& stamp, Array<std::byte>& out) const;
        // 표지가 같고 모양이 맞으면 지금 것을 버리고 되살린다. 모든 페이지가 더러워진다(통째로 올린다). 틀리면 거짓이고 지금 것을 건드리지 않는다.
        bool Restore(ArrayView<const std::byte> baked, const BakedAtlasStamp& expected);
        static std::uint64_t HashFontSource(const std::byte* bytes, std::size_t size);

    private:
        struct Page
        {
            Array<std::byte> pixels;
            std::uint32_t    cursorX = 1;
            std::uint32_t    cursorY = 1;
            std::uint32_t    shelfHeight = 0;
            bool             dirty = false;
            // 새 칸들을 감싸는 사각형이다. 더러움을 지우면 비운다.
            std::uint32_t    dirtyMinX = 0;
            std::uint32_t    dirtyMinY = 0;
            std::uint32_t    dirtyMaxX = 0;
            std::uint32_t    dirtyMaxY = 0;
        };

        static std::uint64_t Key(std::uint32_t pixelSize, GlyphIndex glyph);
        static std::uint64_t SdfKey(std::uint32_t pixelSize, std::uint32_t spread, GlyphIndex glyph);
        AtlasError Place(std::uint64_t key, const GlyphBitmapBox& box, const std::uint8_t* alpha, AtlasGlyph& out);
        bool Allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& page, std::uint32_t& x, std::uint32_t& y);

        std::uint32_t                   m_pageSize = DefaultPageSize;
        Array<Page>                     m_pages;
        Table<std::uint64_t, AtlasGlyph> m_glyphs;
        Array<std::uint8_t>             m_scratch; // 래스터화 버퍼(커버리지 한 채널). 용량을 다시 쓴다
    };
}
