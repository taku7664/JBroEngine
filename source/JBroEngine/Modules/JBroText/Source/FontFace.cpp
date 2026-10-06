#include <JBro/Text/FontFace.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// stb_truetype 의 구현은 이 번역 단위 하나에만 켠다(ThirdParty/README). STBTT_STATIC 으로 이름을 이 파일 안에 가둔다 -
// ImGui 도 같은 라이브러리의 자기 사본을 static 으로 켜므로 링크에서 겹치지 않는다. 래스터라이저(2 단계)와 SDF(4 단계)가 부르는
// STBTT_malloc 은 표준 것이다 - 글리프를 처음 뜰 때만 돈다(아틀라스가 칸을 기억한다).
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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

            Bool Read16(std::size_t at, std::uint16_t& value) const
            {
                if (at + 2 > m_size)
                {
                    return false;
                }
                value = static_cast<std::uint16_t>((m_data[at] << 8) | m_data[at + 1]);
                return true;
            }

            Bool Read32(std::size_t at, UInt32& value) const
            {
                std::uint16_t high = 0;
                std::uint16_t low = 0;
                if (false == Read16(at, high) || false == Read16(at + 2, low))
                {
                    return false;
                }
                value = (static_cast<JBro::UInt32>(high) << 16) | low;
                return true;
            }

            // 커버리지 표 안에서 글리프의 번호다. 없으면 -1.
            Int32 CoverageIndex(std::size_t table, UInt32 glyph) const
            {
                std::uint16_t format = 0;
                std::uint16_t count = 0;
                if (false == Read16(table, format) || false == Read16(table + 2, count))
                {
                    return -1;
                }
                if (format == 1)
                {
                    Int32 low = 0;
                    Int32 high = static_cast<JBro::Int32>(count) - 1;
                    while (low <= high)
                    {
                        const Int32 middle = (low + high) / 2;
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
                    Int32 low = 0;
                    Int32 high = static_cast<JBro::Int32>(count) - 1;
                    while (low <= high)
                    {
                        const Int32 middle = (low + high) / 2;
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
                            return static_cast<JBro::Int32>(first + (glyph - start));
                        }
                    }
                }
                return -1;
            }

            // 글리프의 클래스다. 표에 없으면 0 이다(OpenType 규칙).
            UInt32 ClassOf(std::size_t table, UInt32 glyph) const
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
                    if (Read16(table + 2, start) && Read16(table + 4, count) && glyph >= start && glyph < static_cast<JBro::UInt32>(start) + count
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
                    Int32 low = 0;
                    Int32 high = static_cast<JBro::Int32>(count) - 1;
                    while (low <= high)
                    {
                        const Int32 middle = (low + high) / 2;
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
            Int32 XAdvanceOf(std::size_t record, std::uint16_t valueFormat) const
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
            Bool PairAdjustment(std::size_t subtable, UInt32 left, UInt32 right, Int32& adjustment) const
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
                const Int32 covered = CoverageIndex(subtable + coverage, left);
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
                    Int32 low = 0;
                    Int32 high = static_cast<JBro::Int32>(pairCount) - 1;
                    while (low <= high)
                    {
                        const Int32 middle = (low + high) / 2;
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
                    const UInt32 class1 = ClassOf(subtable + classDef1, left);
                    const UInt32 class2 = ClassOf(subtable + classDef2, right);
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
            Bool Anchor(std::size_t table, Int32& x, Int32& y) const
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
            Bool MarkToBase(std::size_t subtable, UInt32 base, UInt32 mark, Int32& dx, Int32& dy) const
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
                const Int32 markIndex = CoverageIndex(subtable + markCoverage, mark);
                const Int32 baseIndex = CoverageIndex(subtable + baseCoverage, base);
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
                Int32 markX = 0;
                Int32 markY = 0;
                Int32 baseX = 0;
                Int32 baseY = 0;
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
            Bool VisitSubtables(std::size_t gpos, std::uint16_t wanted, TVisit&& visit) const
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
                            UInt32 extensionOffset = 0;
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

            Bool MarkAttachment(std::size_t gpos, UInt32 base, UInt32 mark, Int32& dx, Int32& dy) const
            {
                return VisitSubtables(gpos, 4, [&](std::size_t subtable) { return MarkToBase(subtable, base, mark, dx, dy); });
            }

            // GPOS 전체에서 첫 번째로 찾은 쌍 조정이다. 없으면 0 이다.
            Int32 Kerning(std::size_t gpos, UInt32 left, UInt32 right) const
            {
                Int32 adjustment = 0;
                VisitSubtables(gpos, 2, [&](std::size_t subtable) { return PairAdjustment(subtable, left, right, adjustment); });
                return adjustment;
            }

        private:
            const unsigned char* m_data = nullptr;
            std::size_t          m_size = 0;
        };

        // ── GSUB 옛한글 자모(D-238) ───────────────────────────────────────────────────────────────────────────────────
        // stb 는 GSUB 를 읽지 않는다. 옛한글 폰트(맑은 고딕 등)는 음절의 자모를 `ljmo`·`vjmo`·`tjmo` 의 연쇄 문맥 치환으로 자리에 맞는
        // 변형 글리프로 바꾸고, 바뀐 가운뎃소리·끝소리는 폭이 0 이라 첫소리 위에 겹친다. 그 세 기능만 여기서 직접 읽는다.
        // 읽기 도우미(커버리지·16 비트 읽기)는 GPOS 와 같다. 표가 버퍼 밖을 가리키면 그 부표는 없는 것으로 본다.
        class GsubReader : public GposReader
        {
        public:
            using GposReader::GposReader;

            static constexpr UInt32 Tag(char a, char b, char c, char d)
            {
                return (static_cast<JBro::UInt32>(static_cast<unsigned char>(a)) << 24) | (static_cast<JBro::UInt32>(static_cast<unsigned char>(b)) << 16)
                    | (static_cast<JBro::UInt32>(static_cast<unsigned char>(c)) << 8) | static_cast<JBro::UInt32>(static_cast<unsigned char>(d));
            }

            // `hang` 문자 체계의 기본 언어 체계(없으면 첫 언어 체계)에서 세 기능의 조회 번호를 모아 차례대로 둔다.
            void CollectJamoLookups(std::size_t gsub, Array<std::uint16_t>& lookups) const
            {
                lookups.Clear();
                std::uint16_t major = 0;
                std::uint16_t scriptListOffset = 0;
                std::uint16_t featureListOffset = 0;
                if (false == Read16(gsub, major) || major != 1 || false == Read16(gsub + 4, scriptListOffset)
                    || false == Read16(gsub + 6, featureListOffset))
                {
                    return;
                }
                const std::size_t scriptList = gsub + scriptListOffset;
                const std::size_t featureList = gsub + featureListOffset;
                std::uint16_t scriptCount = 0;
                if (false == Read16(scriptList, scriptCount))
                {
                    return;
                }
                std::size_t script = 0;
                for (std::uint16_t index = 0; index < scriptCount; ++index)
                {
                    const std::size_t record = scriptList + 2 + static_cast<std::size_t>(index) * 6;
                    UInt32 tag = 0;
                    std::uint16_t offset = 0;
                    if (Read32(record, tag) && tag == Tag('h', 'a', 'n', 'g') && Read16(record + 4, offset))
                    {
                        script = scriptList + offset;
                        break;
                    }
                }
                if (script == 0)
                {
                    return;
                }
                std::uint16_t defaultLangSys = 0;
                std::uint16_t langSysCount = 0;
                if (false == Read16(script, defaultLangSys) || false == Read16(script + 2, langSysCount))
                {
                    return;
                }
                std::size_t langSys = 0;
                if (defaultLangSys != 0)
                {
                    langSys = script + defaultLangSys;
                }
                else if (langSysCount > 0)
                {
                    std::uint16_t offset = 0;
                    if (false == Read16(script + 4 + 4, offset))
                    {
                        return;
                    }
                    langSys = script + offset;
                }
                std::uint16_t featureCount = 0;
                if (langSys == 0 || false == Read16(langSys + 4, featureCount))
                {
                    return;
                }
                for (std::uint16_t index = 0; index < featureCount; ++index)
                {
                    std::uint16_t featureIndex = 0;
                    UInt32 tag = 0;
                    std::uint16_t featureOffset = 0;
                    if (false == Read16(langSys + 6 + static_cast<std::size_t>(index) * 2, featureIndex))
                    {
                        return;
                    }
                    const std::size_t record = featureList + 2 + static_cast<std::size_t>(featureIndex) * 6;
                    if (false == Read32(record, tag) || false == Read16(record + 4, featureOffset))
                    {
                        continue;
                    }
                    if (tag != Tag('l', 'j', 'm', 'o') && tag != Tag('v', 'j', 'm', 'o') && tag != Tag('t', 'j', 'm', 'o'))
                    {
                        continue;
                    }
                    const std::size_t feature = featureList + featureOffset;
                    std::uint16_t lookupCount = 0;
                    if (false == Read16(feature + 2, lookupCount))
                    {
                        continue;
                    }
                    for (std::uint16_t lookup = 0; lookup < lookupCount; ++lookup)
                    {
                        std::uint16_t lookupIndex = 0;
                        if (Read16(feature + 4 + static_cast<std::size_t>(lookup) * 2, lookupIndex))
                        {
                            lookups.Add(lookupIndex);
                        }
                    }
                }
                // 여러 기능이 같은 조회를 부를 수 있다. 조회는 목록 차례로 한 번씩 돈다.
                std::sort(lookups.begin(), lookups.end());
                std::uint16_t* last = std::unique(lookups.begin(), lookups.end());
                lookups.Resize(static_cast<std::size_t>(last - lookups.begin()));
            }

            // 조회 하나를 음절 전체에 돈다. depth 는 연쇄 문맥이 부른 조회의 깊이다(한 번까지).
            void ApplyLookup(std::size_t gsub, std::uint16_t lookupIndex, GlyphIndex* glyphs, std::size_t count) const
            {
                for (std::size_t position = 0; position < count; ++position)
                {
                    ApplyAt(gsub, lookupIndex, glyphs, count, position, 0);
                }
            }

        private:
            // 조회 lookupIndex 의 부표를 차례로 대어 보고, 처음 맞는 부표 하나만 position 에 적용한다.
            Bool ApplyAt(std::size_t gsub, std::uint16_t lookupIndex, GlyphIndex* glyphs, std::size_t count, std::size_t position,
                Int32 depth) const
            {
                std::uint16_t lookupListOffset = 0;
                std::uint16_t lookupCount = 0;
                std::uint16_t lookupOffset = 0;
                if (false == Read16(gsub + 8, lookupListOffset))
                {
                    return false;
                }
                const std::size_t list = gsub + lookupListOffset;
                if (false == Read16(list, lookupCount) || lookupIndex >= lookupCount
                    || false == Read16(list + 2 + static_cast<std::size_t>(lookupIndex) * 2, lookupOffset))
                {
                    return false;
                }
                const std::size_t lookup = list + lookupOffset;
                std::uint16_t type = 0;
                std::uint16_t subtableCount = 0;
                if (false == Read16(lookup, type) || false == Read16(lookup + 4, subtableCount))
                {
                    return false;
                }
                for (std::uint16_t subtableIndex = 0; subtableIndex < subtableCount; ++subtableIndex)
                {
                    std::uint16_t subtableOffset = 0;
                    if (false == Read16(lookup + 6 + static_cast<std::size_t>(subtableIndex) * 2, subtableOffset))
                    {
                        return false;
                    }
                    std::size_t subtable = lookup + subtableOffset;
                    std::uint16_t subtableType = type;
                    if (type == 7)
                    {
                        std::uint16_t extensionFormat = 0;
                        UInt32 extensionOffset = 0;
                        if (false == Read16(subtable, extensionFormat) || extensionFormat != 1 || false == Read16(subtable + 2, subtableType)
                            || false == Read32(subtable + 4, extensionOffset))
                        {
                            continue;
                        }
                        subtable += extensionOffset;
                    }
                    if (subtableType == 1 && Single(subtable, glyphs[position]))
                    {
                        return true;
                    }
                    if (subtableType == 6 && depth == 0 && ChainFormat3(gsub, subtable, glyphs, count, position))
                    {
                        return true;
                    }
                }
                return false;
            }

            Bool Single(std::size_t subtable, GlyphIndex& glyph) const
            {
                std::uint16_t format = 0;
                std::uint16_t coverage = 0;
                if (false == Read16(subtable, format) || false == Read16(subtable + 2, coverage))
                {
                    return false;
                }
                const Int32 covered = CoverageIndex(subtable + coverage, glyph);
                if (covered < 0)
                {
                    return false;
                }
                std::uint16_t value = 0;
                if (format == 1 && Read16(subtable + 4, value))
                {
                    glyph = static_cast<GlyphIndex>((glyph + value) & 0xFFFF);
                    return true;
                }
                std::uint16_t substituteCount = 0;
                if (format == 2 && Read16(subtable + 4, substituteCount) && covered < substituteCount
                    && Read16(subtable + 6 + static_cast<std::size_t>(covered) * 2, value))
                {
                    glyph = value;
                    return true;
                }
                return false;
            }

            // 연쇄 문맥 치환 형식 3: 앞(가까운 것부터)·입력·뒤가 커버리지마다 맞으면 치환 기록의 조회를 그 자리에 부른다.
            Bool ChainFormat3(std::size_t gsub, std::size_t subtable, GlyphIndex* glyphs, std::size_t count, std::size_t position) const
            {
                std::uint16_t format = 0;
                if (false == Read16(subtable, format) || format != 3)
                {
                    return false;
                }
                std::size_t cursor = subtable + 2;
                std::uint16_t backtrackCount = 0;
                if (false == Read16(cursor, backtrackCount) || backtrackCount > position)
                {
                    return false;
                }
                for (std::uint16_t index = 0; index < backtrackCount; ++index)
                {
                    std::uint16_t offset = 0;
                    if (false == Read16(cursor + 2 + static_cast<std::size_t>(index) * 2, offset)
                        || CoverageIndex(subtable + offset, glyphs[position - 1 - index]) < 0)
                    {
                        return false;
                    }
                }
                cursor += 2 + static_cast<std::size_t>(backtrackCount) * 2;
                std::uint16_t inputCount = 0;
                if (false == Read16(cursor, inputCount) || inputCount == 0 || position + inputCount > count)
                {
                    return false;
                }
                for (std::uint16_t index = 0; index < inputCount; ++index)
                {
                    std::uint16_t offset = 0;
                    if (false == Read16(cursor + 2 + static_cast<std::size_t>(index) * 2, offset)
                        || CoverageIndex(subtable + offset, glyphs[position + index]) < 0)
                    {
                        return false;
                    }
                }
                cursor += 2 + static_cast<std::size_t>(inputCount) * 2;
                std::uint16_t lookaheadCount = 0;
                if (false == Read16(cursor, lookaheadCount) || position + inputCount + lookaheadCount > count)
                {
                    return false;
                }
                for (std::uint16_t index = 0; index < lookaheadCount; ++index)
                {
                    std::uint16_t offset = 0;
                    if (false == Read16(cursor + 2 + static_cast<std::size_t>(index) * 2, offset)
                        || CoverageIndex(subtable + offset, glyphs[position + inputCount + index]) < 0)
                    {
                        return false;
                    }
                }
                cursor += 2 + static_cast<std::size_t>(lookaheadCount) * 2;
                std::uint16_t substitutionCount = 0;
                if (false == Read16(cursor, substitutionCount))
                {
                    return false;
                }
                for (std::uint16_t index = 0; index < substitutionCount; ++index)
                {
                    std::uint16_t sequenceIndex = 0;
                    std::uint16_t lookupIndex = 0;
                    if (false == Read16(cursor + 2 + static_cast<std::size_t>(index) * 4, sequenceIndex)
                        || false == Read16(cursor + 4 + static_cast<std::size_t>(index) * 4, lookupIndex) || sequenceIndex >= inputCount)
                    {
                        continue;
                    }
                    ApplyAt(gsub, lookupIndex, glyphs, count, position + sequenceIndex, 1);
                }
                return true;
            }
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
        m_gsub = other.m_gsub;
        m_jamoLookups = static_cast<Array<std::uint16_t>&&>(other.m_jamoLookups);
        std::memcpy(m_info, other.m_info, sizeof(m_info));
        other.m_gsub = 0;
        other.m_metrics = {};
        other.m_loaded = false;
        std::memset(other.m_info, 0, sizeof(other.m_info));
    }

    Bool FontFace::Load(ArrayView<const std::byte> bytes, UInt32 faceIndex)
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
        const Int32 offset = stbtt_GetFontOffsetForIndex(data, static_cast<JBro::Int32>(faceIndex));
        if (offset < 0 || 0 == stbtt_InitFont(Info(m_info), data, offset))
        {
            Unload();
            return false;
        }

        const stbtt_fontinfo* info = Info(m_info);
        const Float emScale = stbtt_ScaleForMappingEmToPixels(info, 1.0f);
        if (false == (emScale > 0.0f))
        {
            Unload();
            return false;
        }
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
        m_metrics.unitsPerEm = static_cast<JBro::Int32>(std::lround(1.0f / emScale));
        m_metrics.ascent = ascent;
        m_metrics.descent = descent;
        m_metrics.lineGap = lineGap;
        // 옛한글 자모 기능의 조회를 한 번 모은다. 표가 없거나 기능이 없으면 비어 있다.
        m_gsub = stbtt__find_table(reinterpret_cast<unsigned char*>(m_bytes.Data()), static_cast<stbtt_uint32>(offset), "GSUB");
        if (m_gsub != 0)
        {
            const GsubReader reader(data, m_bytes.Size());
            reader.CollectJamoLookups(m_gsub, m_jamoLookups);
        }
        m_loaded = true;
        return true;
    }

    void FontFace::Unload()
    {
        m_bytes.Clear();
        m_metrics = {};
        m_loaded = false;
        m_gsub = 0;
        m_jamoLookups.Clear();
        std::memset(m_info, 0, sizeof(m_info));
    }

    Bool FontFace::IsLoaded() const
    {
        return m_loaded;
    }

    GlyphIndex FontFace::FindGlyph(char32_t codepoint) const
    {
        if (false == m_loaded)
        {
            return MissingGlyph;
        }
        const Int32 glyph = stbtt_FindGlyphIndex(Info(m_info), static_cast<JBro::Int32>(codepoint));
        return glyph > 0 ? static_cast<GlyphIndex>(glyph) : MissingGlyph;
    }

    const FontMetrics& FontFace::GetMetrics() const
    {
        return m_metrics;
    }

    Int32 FontFace::GetAdvance(GlyphIndex glyph) const
    {
        if (false == m_loaded)
        {
            return 0;
        }
        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetGlyphHMetrics(Info(m_info), static_cast<JBro::Int32>(glyph), &advance, &leftSideBearing);
        return advance;
    }

    Int32 FontFace::GetKerning(GlyphIndex left, GlyphIndex right) const
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
            return stbtt__GetGlyphKernInfoAdvance(info, static_cast<JBro::Int32>(left), static_cast<JBro::Int32>(right));
        }
        return 0;
    }

    Bool FontFace::GetMarkAttachment(GlyphIndex base, GlyphIndex mark, Int32& dx, Int32& dy) const
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

    Bool FontFace::HasHangulJamoShaping() const
    {
        return m_loaded && m_gsub != 0 && false == m_jamoLookups.IsEmpty();
    }

    Bool FontFace::ShapeHangulJamo(GlyphIndex* glyphs, std::size_t count) const
    {
        if (false == HasHangulJamoShaping() || glyphs == nullptr || count == 0)
        {
            return false;
        }
        const GsubReader reader(reinterpret_cast<const unsigned char*>(m_bytes.Data()), m_bytes.Size());
        for (const std::uint16_t lookup : m_jamoLookups)
        {
            reader.ApplyLookup(m_gsub, lookup, glyphs, count);
        }
        return true;
    }

    GlyphBox FontFace::GetGlyphBox(GlyphIndex glyph) const
    {
        GlyphBox box;
        if (false == m_loaded)
        {
            return box;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<JBro::Int32>(glyph)))
        {
            return box;
        }
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (0 == stbtt_GetGlyphBox(info, static_cast<JBro::Int32>(glyph), &x0, &y0, &x1, &y1))
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

    Bool FontFace::MeasureGlyphBitmap(GlyphIndex glyph, Float pixelSize, GlyphBitmapBox& box) const
    {
        box = {};
        if (false == m_loaded || false == (pixelSize > 0.0f))
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<JBro::Int32>(glyph)))
        {
            return true;
        }
        const Float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        // stb 의 상자는 y 가 아래쪽이다. 위쪽이 양수인 top 으로 뒤집는다.
        stbtt_GetGlyphBitmapBox(info, static_cast<JBro::Int32>(glyph), scale, scale, &x0, &y0, &x1, &y1);
        box.left = x0;
        box.top = -y0;
        box.width = x1 > x0 ? Int32(x1 - x0) : Int32(0);
        box.height = y1 > y0 ? Int32(y1 - y0) : Int32(0);
        return true;
    }

    Bool FontFace::RasterizeGlyph(GlyphIndex glyph, Float pixelSize, const GlyphBitmapBox& box, std::uint8_t* coverage, Int32 stride) const
    {
        if (false == m_loaded || coverage == nullptr || box.width <= 0 || box.height <= 0 || stride < box.width || false == (pixelSize > 0.0f))
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        const Float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        stbtt_MakeGlyphBitmap(info, coverage, box.width, box.height, stride, scale, scale, static_cast<JBro::Int32>(glyph));
        return true;
    }

    Bool FontFace::RasterizeGlyphSdf(GlyphIndex glyph, Float pixelSize, Int32 spread, GlyphBitmapBox& box,
        Array<std::uint8_t>& distances) const
    {
        box = {};
        if (false == m_loaded || false == (pixelSize > 0.0f) || spread <= 0)
        {
            return false;
        }
        const stbtt_fontinfo* info = Info(m_info);
        if (0 != stbtt_IsGlyphEmpty(info, static_cast<JBro::Int32>(glyph)))
        {
            return true;
        }
        const Float scale = stbtt_ScaleForMappingEmToPixels(info, pixelSize);
        constexpr unsigned char OnEdge = 128;
        const Float perPixel = static_cast<JBro::Float>(OnEdge) / static_cast<JBro::Float>(spread);
        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        unsigned char* field = stbtt_GetGlyphSDF(info, scale, static_cast<JBro::Int32>(glyph), spread, OnEdge, perPixel,
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
