#include <JBro/Text/FontFace.h>

#include <cmath>
#include <cstring>
#include <limits>

// stb_truetype 의 구현은 이 번역 단위 하나에만 켠다(ThirdParty/README). STBTT_STATIC 으로 이름을 이 파일 안에 가둔다 -
// ImGui 도 같은 라이브러리의 자기 사본을 static 으로 켜므로 링크에서 겹치지 않는다. 래스터라이저와 SDF 는 아직 쓰지 않는다
// (아틀라스는 2 단계, text-plan §5). 그쪽이 부르는 STBTT_malloc 은 표준 것이다 - 글리프를 처음 뜰 때만 돈다.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace JBro::Text
{
    namespace
    {
        static_assert(sizeof(stbtt_fontinfo) <= 192, "FontFace::InfoStorageSize must hold stbtt_fontinfo");
        static_assert(alignof(stbtt_fontinfo) <= 8, "FontFace::m_info is aligned to 8");

        stbtt_fontinfo* Info(unsigned char* storage)
        {
            return reinterpret_cast<stbtt_fontinfo*>(storage);
        }

        const stbtt_fontinfo* Info(const unsigned char* storage)
        {
            return reinterpret_cast<const stbtt_fontinfo*>(storage);
        }
    }

    FontFace::FontFace(FontFace&& other) noexcept
    {
        MoveFrom(other);
    }

    FontFace& FontFace::operator=(FontFace&& other) noexcept
    {
        if (this != &other)
        {
            MoveFrom(other);
        }
        return *this;
    }

    void FontFace::MoveFrom(FontFace& other) noexcept
    {
        // stbtt_fontinfo 는 포인터와 정수뿐인 POD 다. 바이트 버퍼는 Array 의 이동으로 같은 주소째 넘어오므로
        // 안의 포인터를 고칠 필요가 없다.
        m_bytes = static_cast<Array<std::byte>&&>(other.m_bytes);
        m_metrics = other.m_metrics;
        m_loaded = other.m_loaded;
        std::memcpy(m_info, other.m_info, sizeof(m_info));
        other.m_metrics = {};
        other.m_loaded = false;
        std::memset(other.m_info, 0, sizeof(other.m_info));
    }

    bool FontFace::Load(ArrayView<const std::byte> bytes, std::uint32_t faceIndex)
    {
        Unload();
        // sfnt 머리(12 바이트)도 없으면 stb 가 표를 찾다 버퍼 밖을 읽는다.
        if (bytes.Data() == nullptr || bytes.Size() < 12 || bytes.Size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        m_bytes.Resize(bytes.Size());
        std::memcpy(m_bytes.Data(), bytes.Data(), bytes.Size());

        const unsigned char* data = reinterpret_cast<const unsigned char*>(m_bytes.Data());
        const int offset = stbtt_GetFontOffsetForIndex(data, static_cast<int>(faceIndex));
        if (offset < 0 || 0 == stbtt_InitFont(Info(m_info), data, offset))
        {
            Unload();
            return false;
        }

        const stbtt_fontinfo* info = Info(m_info);
        const float emScale = stbtt_ScaleForMappingEmToPixels(info, 1.0f);
        if (false == (emScale > 0.0f))
        {
            Unload();
            return false;
        }
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
        m_metrics.unitsPerEm = static_cast<std::int32_t>(std::lround(1.0f / emScale));
        m_metrics.ascent = ascent;
        m_metrics.descent = descent;
        m_metrics.lineGap = lineGap;
        m_loaded = true;
        return true;
    }

    void FontFace::Unload()
    {
        m_bytes.Clear();
        m_metrics = {};
        m_loaded = false;
        std::memset(m_info, 0, sizeof(m_info));
    }

    bool FontFace::IsLoaded() const
    {
        return m_loaded;
    }

    GlyphIndex FontFace::FindGlyph(char32_t codepoint) const
    {
        if (false == m_loaded)
        {
            return MissingGlyph;
        }
        const int glyph = stbtt_FindGlyphIndex(Info(m_info), static_cast<int>(codepoint));
        return glyph > 0 ? static_cast<GlyphIndex>(glyph) : MissingGlyph;
    }

    const FontMetrics& FontFace::GetMetrics() const
    {
        return m_metrics;
    }

    std::int32_t FontFace::GetAdvance(GlyphIndex glyph) const
    {
        if (false == m_loaded)
        {
            return 0;
        }
        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetGlyphHMetrics(Info(m_info), static_cast<int>(glyph), &advance, &leftSideBearing);
        return advance;
    }

    std::int32_t FontFace::GetKerning(GlyphIndex left, GlyphIndex right) const
    {
        if (false == m_loaded)
        {
            return 0;
        }
        return stbtt_GetGlyphKernAdvance(Info(m_info), static_cast<int>(left), static_cast<int>(right));
    }

    GlyphBox FontFace::GetGlyphBox(GlyphIndex glyph) const
    {
        GlyphBox box;
        if (false == m_loaded)
        {
            return box;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<int>(glyph)))
        {
            return box;
        }
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (0 == stbtt_GetGlyphBox(info, static_cast<int>(glyph), &x0, &y0, &x1, &y1))
        {
            return box;
        }
        box.minX = x0;
        box.minY = y0;
        box.maxX = x1;
        box.maxY = y1;
        box.empty = false;
        return box;
    }
}
