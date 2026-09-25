#include <JBro/Text/FontFace.h>

#include <cmath>
#include <cstring>
#include <limits>

// stb_truetype 의 구현은 이 번역 단위 하나에만 켠다(ThirdParty/README). STBTT_STATIC 으로 이름을 이 파일 안에 가둔다 -
// ImGui 도 같은 라이브러리의 자기 사본을 static 으로 켜므로 링크에서 겹치지 않는다. 래스터라이저(2 단계)와 SDF(4 단계)가 부르는
// STBTT_malloc 은 표준 것이다 - 글리프를 처음 뜰 때만 돈다(아틀라스가 칸을 기억한다).
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

        // ── GPOS 쌍 조정(text-plan §7) ────────────────────────────────────────────────────────────────────────────────
        // stb 는 확장 조회(형식 9)로 감싼 쌍 조정과, 첫 값 형식이 XAdvance 하나가 아닌 쌍 조정을 건너뛰어 그 폰트의 커닝이 **조용히
        // 0** 이었다. 큰 폰트는 64 KB 오프셋을 피하려고 확장 조회를 흔히 쓴다. 쌍 조정(형식 2, 부표 형식 1·2)을 여기서 직접 읽는다 -
        // 값 형식은 어떤 조합이든 XAdvance 자리를 찾아 읽고, 확장 조회는 풀어서 읽는다. 표가 버퍼 밖을 가리키면 그 부표는 없는 것으로 본다.
        // 결합 표시의 자리(mark-to-base, 형식 4)도 같은 조회 순회로 읽는다 - stb 는 이 형식을 아예 읽지 않는다.
        class GposReader
        {
        public:
            GposReader(const unsigned char* data, std::size_t size)
                : m_data(data)
                , m_size(size)
            {
            }

            bool Read16(std::size_t at, std::uint16_t& value) const
            {
                if (at + 2 > m_size)
                {
                    return false;
                }
                value = static_cast<std::uint16_t>((m_data[at] << 8) | m_data[at + 1]);
                return true;
            }

            bool Read32(std::size_t at, std::uint32_t& value) const
            {
                std::uint16_t high = 0;
                std::uint16_t low = 0;
                if (false == Read16(at, high) || false == Read16(at + 2, low))
                {
                    return false;
                }
                value = (static_cast<std::uint32_t>(high) << 16) | low;
                return true;
            }

            // 커버리지 표 안에서 글리프의 번호다. 없으면 -1.
            std::int32_t CoverageIndex(std::size_t table, std::uint32_t glyph) const
            {
                std::uint16_t format = 0;
                std::uint16_t count = 0;
                if (false == Read16(table, format) || false == Read16(table + 2, count))
                {
                    return -1;
                }
                if (format == 1)
                {
                    std::int32_t low = 0;
                    std::int32_t high = static_cast<std::int32_t>(count) - 1;
                    while (low <= high)
                    {
                        const std::int32_t middle = (low + high) / 2;
                        std::uint16_t value = 0;
                        if (false == Read16(table + 4 + static_cast<std::size_t>(middle) * 2, value))
                        {
                            return -1;
                        }
                        if (value == glyph)
                        {
                            return middle;
                        }
                        if (value < glyph)
                        {
                            low = middle + 1;
                        }
                        else
                        {
                            high = middle - 1;
                        }
                    }
                    return -1;
                }
                if (format == 2)
                {
                    std::int32_t low = 0;
                    std::int32_t high = static_cast<std::int32_t>(count) - 1;
                    while (low <= high)
                    {
                        const std::int32_t middle = (low + high) / 2;
                        const std::size_t record = table + 4 + static_cast<std::size_t>(middle) * 6;
                        std::uint16_t start = 0;
                        std::uint16_t end = 0;
                        std::uint16_t first = 0;
                        if (false == Read16(record, start) || false == Read16(record + 2, end) || false == Read16(record + 4, first))
                        {
                            return -1;
                        }
                        if (glyph < start)
                        {
                            high = middle - 1;
                        }
                        else if (glyph > end)
                        {
                            low = middle + 1;
                        }
                        else
                        {
                            return static_cast<std::int32_t>(first + (glyph - start));
                        }
                    }
                }
                return -1;
            }

            // 글리프의 클래스다. 표에 없으면 0 이다(OpenType 규칙).
            std::uint32_t ClassOf(std::size_t table, std::uint32_t glyph) const
            {
                std::uint16_t format = 0;
                if (false == Read16(table, format))
                {
                    return 0;
                }
                if (format == 1)
                {
                    std::uint16_t start = 0;
                    std::uint16_t count = 0;
                    std::uint16_t value = 0;
                    if (Read16(table + 2, start) && Read16(table + 4, count) && glyph >= start && glyph < static_cast<std::uint32_t>(start) + count
                        && Read16(table + 6 + static_cast<std::size_t>(glyph - start) * 2, value))
                    {
                        return value;
                    }
                    return 0;
                }
                if (format == 2)
                {
                    std::uint16_t count = 0;
                    if (false == Read16(table + 2, count))
                    {
                        return 0;
                    }
                    std::int32_t low = 0;
                    std::int32_t high = static_cast<std::int32_t>(count) - 1;
                    while (low <= high)
                    {
                        const std::int32_t middle = (low + high) / 2;
                        const std::size_t record = table + 4 + static_cast<std::size_t>(middle) * 6;
                        std::uint16_t start = 0;
                        std::uint16_t end = 0;
                        std::uint16_t value = 0;
                        if (false == Read16(record, start) || false == Read16(record + 2, end) || false == Read16(record + 4, value))
                        {
                            return 0;
                        }
                        if (glyph < start)
                        {
                            high = middle - 1;
                        }
                        else if (glyph > end)
                        {
                            low = middle + 1;
                        }
                        else
                        {
                            return value;
                        }
                    }
                }
                return 0;
            }

            // 값 기록 안의 XAdvance 다. 형식에 XAdvance 가 없으면 0 이다. 앞의 XPlacement·YPlacement 만큼 건너뛴다.
            std::int32_t XAdvanceOf(std::size_t record, std::uint16_t valueFormat) const
            {
                if ((valueFormat & 0x0004) == 0)
                {
                    return 0;
                }
                const std::size_t skip = ((valueFormat & 0x0001) != 0 ? 2 : 0) + ((valueFormat & 0x0002) != 0 ? 2 : 0);
                std::uint16_t value = 0;
                return Read16(record + skip, value) ? static_cast<std::int16_t>(value) : 0;
            }

            static std::size_t ValueSize(std::uint16_t valueFormat)
            {
                std::size_t size = 0;
                for (std::uint16_t bit = 1; bit != 0 && bit <= 0x0080; bit = static_cast<std::uint16_t>(bit << 1))
                {
                    size += (valueFormat & bit) != 0 ? 2 : 0;
                }
                return size;
            }

            // 쌍 조정 부표 하나다. 찾으면 참이고 조정값(폰트 단위)을 준다.
            bool PairAdjustment(std::size_t subtable, std::uint32_t left, std::uint32_t right, std::int32_t& adjustment) const
            {
                std::uint16_t format = 0;
                std::uint16_t coverage = 0;
                std::uint16_t valueFormat1 = 0;
                std::uint16_t valueFormat2 = 0;
                if (false == Read16(subtable, format) || false == Read16(subtable + 2, coverage)
                    || false == Read16(subtable + 4, valueFormat1) || false == Read16(subtable + 6, valueFormat2))
                {
                    return false;
                }
                const std::int32_t covered = CoverageIndex(subtable + coverage, left);
                if (covered < 0)
                {
                    return false;
                }
                const std::size_t size1 = ValueSize(valueFormat1);
                const std::size_t size2 = ValueSize(valueFormat2);
                if (format == 1)
                {
                    std::uint16_t setCount = 0;
                    std::uint16_t setOffset = 0;
                    if (false == Read16(subtable + 8, setCount) || covered >= setCount
                        || false == Read16(subtable + 10 + static_cast<std::size_t>(covered) * 2, setOffset))
                    {
                        return false;
                    }
                    const std::size_t set = subtable + setOffset;
                    std::uint16_t pairCount = 0;
                    if (false == Read16(set, pairCount))
                    {
                        return false;
                    }
                    const std::size_t recordSize = 2 + size1 + size2;
                    std::int32_t low = 0;
                    std::int32_t high = static_cast<std::int32_t>(pairCount) - 1;
                    while (low <= high)
                    {
                        const std::int32_t middle = (low + high) / 2;
                        const std::size_t record = set + 2 + static_cast<std::size_t>(middle) * recordSize;
                        std::uint16_t second = 0;
                        if (false == Read16(record, second))
                        {
                            return false;
                        }
                        if (second == right)
                        {
                            adjustment = XAdvanceOf(record + 2, valueFormat1);
                            return true;
                        }
                        if (second < right)
                        {
                            low = middle + 1;
                        }
                        else
                        {
                            high = middle - 1;
                        }
                    }
                    return false;
                }
                if (format == 2)
                {
                    std::uint16_t classDef1 = 0;
                    std::uint16_t classDef2 = 0;
                    std::uint16_t class1Count = 0;
                    std::uint16_t class2Count = 0;
                    if (false == Read16(subtable + 8, classDef1) || false == Read16(subtable + 10, classDef2)
                        || false == Read16(subtable + 12, class1Count) || false == Read16(subtable + 14, class2Count))
                    {
                        return false;
                    }
                    const std::uint32_t class1 = ClassOf(subtable + classDef1, left);
                    const std::uint32_t class2 = ClassOf(subtable + classDef2, right);
                    if (class1 >= class1Count || class2 >= class2Count)
                    {
                        return false;
                    }
                    const std::size_t record = subtable + 16
                        + (static_cast<std::size_t>(class1) * class2Count + class2) * (size1 + size2);
                    adjustment = XAdvanceOf(record, valueFormat1);
                    // 클래스 쌍 표는 모든 쌍을 담으므로 0 도 "찾았다" 다. 0 이 아닐 때만 끝낸다 - 다음 조회가 이 쌍을 다룰 수 있다.
                    return adjustment != 0;
                }
                return false;
            }

            // 앵커 표의 (x, y) 다. 형식 1·2·3 모두 앞의 두 값이 좌표다(2 의 윤곽 점, 3 의 장치 표는 쓰지 않는다).
            bool Anchor(std::size_t table, std::int32_t& x, std::int32_t& y) const
            {
                std::uint16_t format = 0;
                std::uint16_t xValue = 0;
                std::uint16_t yValue = 0;
                if (false == Read16(table, format) || format < 1 || format > 3 || false == Read16(table + 2, xValue)
                    || false == Read16(table + 4, yValue))
                {
                    return false;
                }
                x = static_cast<std::int16_t>(xValue);
                y = static_cast<std::int16_t>(yValue);
                return true;
            }

            // mark-to-base 부표(형식 4 의 형식 1) 하나다. 찾으면 받침 원점에서 표시 원점까지의 거리(폰트 단위)를 준다.
            bool MarkToBase(std::size_t subtable, std::uint32_t base, std::uint32_t mark, std::int32_t& dx, std::int32_t& dy) const
            {
                std::uint16_t format = 0;
                std::uint16_t markCoverage = 0;
                std::uint16_t baseCoverage = 0;
                std::uint16_t classCount = 0;
                std::uint16_t markArray = 0;
                std::uint16_t baseArray = 0;
                if (false == Read16(subtable, format) || format != 1 || false == Read16(subtable + 2, markCoverage)
                    || false == Read16(subtable + 4, baseCoverage) || false == Read16(subtable + 6, classCount)
                    || false == Read16(subtable + 8, markArray) || false == Read16(subtable + 10, baseArray))
                {
                    return false;
                }
                const std::int32_t markIndex = CoverageIndex(subtable + markCoverage, mark);
                const std::int32_t baseIndex = CoverageIndex(subtable + baseCoverage, base);
                if (markIndex < 0 || baseIndex < 0)
                {
                    return false;
                }
                const std::size_t marks = subtable + markArray;
                std::uint16_t markClass = 0;
                std::uint16_t markAnchor = 0;
                if (false == Read16(marks + 2 + static_cast<std::size_t>(markIndex) * 4, markClass)
                    || false == Read16(marks + 4 + static_cast<std::size_t>(markIndex) * 4, markAnchor) || markClass >= classCount)
                {
                    return false;
                }
                const std::size_t bases = subtable + baseArray;
                std::uint16_t baseAnchor = 0;
                if (false == Read16(bases + 2 + (static_cast<std::size_t>(baseIndex) * classCount + markClass) * 2, baseAnchor)
                    || baseAnchor == 0)
                {
                    return false;
                }
                std::int32_t markX = 0;
                std::int32_t markY = 0;
                std::int32_t baseX = 0;
                std::int32_t baseY = 0;
                if (false == Anchor(marks + markAnchor, markX, markY) || false == Anchor(bases + baseAnchor, baseX, baseY))
                {
                    return false;
                }
                dx = baseX - markX;
                dy = baseY - markY;
                return true;
            }

            // 조회 목록을 돌며 형식 wanted 의 부표를 찾는다(확장 조회는 풀어서). visit 가 참을 주면 멈추고 참이다.
            template <typename TVisit>
            bool VisitSubtables(std::size_t gpos, std::uint16_t wanted, TVisit&& visit) const
            {
                std::uint16_t major = 0;
                std::uint16_t lookupList = 0;
                if (false == Read16(gpos, major) || major != 1 || false == Read16(gpos + 8, lookupList))
                {
                    return false;
                }
                const std::size_t list = gpos + lookupList;
                std::uint16_t lookupCount = 0;
                if (false == Read16(list, lookupCount))
                {
                    return false;
                }
                for (std::uint16_t lookupIndex = 0; lookupIndex < lookupCount; ++lookupIndex)
                {
                    std::uint16_t lookupOffset = 0;
                    std::uint16_t type = 0;
                    std::uint16_t subtableCount = 0;
                    if (false == Read16(list + 2 + static_cast<std::size_t>(lookupIndex) * 2, lookupOffset))
                    {
                        return false;
                    }
                    const std::size_t lookup = list + lookupOffset;
                    if (false == Read16(lookup, type) || false == Read16(lookup + 4, subtableCount) || (type != wanted && type != 9))
                    {
                        continue;
                    }
                    for (std::uint16_t subtableIndex = 0; subtableIndex < subtableCount; ++subtableIndex)
                    {
                        std::uint16_t subtableOffset = 0;
                        if (false == Read16(lookup + 6 + static_cast<std::size_t>(subtableIndex) * 2, subtableOffset))
                        {
                            break;
                        }
                        std::size_t subtable = lookup + subtableOffset;
                        if (type == 9)
                        {
                            std::uint16_t extensionFormat = 0;
                            std::uint16_t extensionType = 0;
                            std::uint32_t extensionOffset = 0;
                            if (false == Read16(subtable, extensionFormat) || extensionFormat != 1
                                || false == Read16(subtable + 2, extensionType) || extensionType != wanted
                                || false == Read32(subtable + 4, extensionOffset))
                            {
                                continue;
                            }
                            subtable += extensionOffset;
                        }
                        if (visit(subtable))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool MarkAttachment(std::size_t gpos, std::uint32_t base, std::uint32_t mark, std::int32_t& dx, std::int32_t& dy) const
            {
                return VisitSubtables(gpos, 4, [&](std::size_t subtable) { return MarkToBase(subtable, base, mark, dx, dy); });
            }

            // GPOS 전체에서 첫 번째로 찾은 쌍 조정이다. 없으면 0 이다.
            std::int32_t Kerning(std::size_t gpos, std::uint32_t left, std::uint32_t right) const
            {
                std::int32_t adjustment = 0;
                VisitSubtables(gpos, 2, [&](std::size_t subtable) { return PairAdjustment(subtable, left, right, adjustment); });
                return adjustment;
            }

        private:
            const unsigned char* m_data = nullptr;
            std::size_t          m_size = 0;
        };
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
        const stbtt_fontinfo* info = Info(m_info);
        // GPOS 가 있으면 우리 읽기(확장 조회·넓은 값 형식까지)다. 없으면 옛 `kern` 표를 stb 가 읽는다 - stb 도 GPOS 가 있으면 `kern` 을 보지 않는다.
        if (info->gpos != 0)
        {
            const GposReader reader(reinterpret_cast<const unsigned char*>(m_bytes.Data()), m_bytes.Size());
            return reader.Kerning(static_cast<std::size_t>(info->gpos), left, right);
        }
        if (info->kern != 0)
        {
            return stbtt__GetGlyphKernInfoAdvance(info, static_cast<int>(left), static_cast<int>(right));
        }
        return 0;
    }

    bool FontFace::GetMarkAttachment(GlyphIndex base, GlyphIndex mark, std::int32_t& dx, std::int32_t& dy) const
    {
        dx = 0;
        dy = 0;
        if (false == m_loaded)
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (info->gpos == 0)
        {
            return false;
        }
        const GposReader reader(reinterpret_cast<const unsigned char*>(m_bytes.Data()), m_bytes.Size());
        return reader.MarkAttachment(static_cast<std::size_t>(info->gpos), base, mark, dx, dy);
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

    bool FontFace::MeasureGlyphBitmap(GlyphIndex glyph, float pixelSize, GlyphBitmapBox& box) const
    {
        box = {};
        if (false == m_loaded || false == (pixelSize > 0.0f))
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<int>(glyph)))
        {
            return true;
        }
        const float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        // stb 의 상자는 y 가 아래쪽이다. 위쪽이 양수인 top 으로 뒤집는다.
        stbtt_GetGlyphBitmapBox(info, static_cast<int>(glyph), scale, scale, &x0, &y0, &x1, &y1);
        box.left = x0;
        box.top = -y0;
        box.width = x1 > x0 ? x1 - x0 : 0;
        box.height = y1 > y0 ? y1 - y0 : 0;
        return true;
    }

    bool FontFace::RasterizeGlyph(GlyphIndex glyph, float pixelSize, const GlyphBitmapBox& box, std::uint8_t* coverage, std::int32_t stride) const
    {
        if (false == m_loaded || coverage == nullptr || box.width <= 0 || box.height <= 0 || stride < box.width || false == (pixelSize > 0.0f))
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        const float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        stbtt_MakeGlyphBitmap(info, coverage, box.width, box.height, stride, scale, scale, static_cast<int>(glyph));
        return true;
    }

    bool FontFace::RasterizeGlyphSdf(GlyphIndex glyph, float pixelSize, std::int32_t spread, GlyphBitmapBox& box,
        Array<std::uint8_t>& distances) const
    {
        box = {};
        if (false == m_loaded || false == (pixelSize > 0.0f) || spread <= 0)
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<int>(glyph)))
        {
            return true;
        }
        const float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        constexpr unsigned char OnEdge = 128;
        const float perPixel = static_cast<float>(OnEdge) / static_cast<float>(spread);
        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        unsigned char* field = stbtt_GetGlyphSDF(info, scale, static_cast<int>(glyph), spread, OnEdge, perPixel,
            &width, &height, &xoff, &yoff);
        if (field == nullptr || width <= 0 || height <= 0)
        {
            stbtt_FreeSDF(field, nullptr);
            return true;
        }
        distances.Resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        std::memcpy(distances.Data(), field, distances.Size());
        stbtt_FreeSDF(field, nullptr);
        // stb 의 오프셋은 y 가 아래쪽이다. 위쪽이 양수인 top 으로 뒤집는다.
        box.left = xoff;
        box.top = -yoff;
        box.width = width;
        box.height = height;
        return true;
    }
}
