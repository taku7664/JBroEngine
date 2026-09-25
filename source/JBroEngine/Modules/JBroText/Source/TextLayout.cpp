#include <JBro/Text/TextLayout.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace JBro::Text
{
    namespace
    {
        constexpr char32_t ReplacementCharacter = 0xFFFD;

        // 잘못된 바이트열은 U+FFFD 하나가 된다. 끊긴 열은 이어지던 바이트까지 하나로 먹고(WHATWG 의 "최대 부분열"),
        // 다음 글자의 첫 바이트는 남긴다 - 뒤의 바른 글자를 잃지 않게 한다.
        // 겹친 인코딩(overlong)·서로게이트·U+10FFFF 초과는 열 전체가 U+FFFD 하나다.
        char32_t DecodeOne(const char* text, std::size_t length, std::size_t& cursor)
        {
            const unsigned char first = static_cast<unsigned char>(text[cursor]);
            if (first < 0x80)
            {
                ++cursor;
                return first;
            }
            std::size_t count = 0;
            char32_t value = 0;
            char32_t minimum = 0;
            if ((first & 0xE0) == 0xC0)
            {
                count = 1;
                value = first & 0x1F;
                minimum = 0x80;
            }
            else if ((first & 0xF0) == 0xE0)
            {
                count = 2;
                value = first & 0x0F;
                minimum = 0x800;
            }
            else if ((first & 0xF8) == 0xF0)
            {
                count = 3;
                value = first & 0x07;
                minimum = 0x10000;
            }
            else
            {
                ++cursor;
                return ReplacementCharacter;
            }
            for (std::size_t index = 1; index <= count; ++index)
            {
                // 끝에서 잘렸거나 이어지는 바이트가 아니다. 거기까지가 U+FFFD 하나다.
                if (cursor + index >= length || (static_cast<unsigned char>(text[cursor + index]) & 0xC0) != 0x80)
                {
                    cursor += index;
                    return ReplacementCharacter;
                }
                value = (value << 6) | (static_cast<unsigned char>(text[cursor + index]) & 0x3F);
            }
            if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            {
                cursor += count + 1;
                return ReplacementCharacter;
            }
            cursor += count + 1;
            return value;
        }

        // 현대 한글 조합형 자모(첫소리 19·가운뎃소리 21·끝소리 27)다. 분해된 입력(NFD, macOS 파일 이름 등)을 음절로
        // 되돌리는 데 쓴다. 조합은 표 없이 산술로 된다(유니코드 3.12 절). 옛한글은 조합하지 않는다.
        constexpr char32_t HangulBase = 0xAC00;
        constexpr char32_t LeadBase = 0x1100;
        constexpr char32_t VowelBase = 0x1161;
        constexpr char32_t TrailBase = 0x11A7;
        constexpr char32_t LeadCount = 19;
        constexpr char32_t VowelCount = 21;
        constexpr char32_t TrailCount = 28;
        constexpr char32_t SyllableCount = LeadCount * VowelCount * TrailCount;

        bool IsLead(char32_t value)
        {
            return value >= LeadBase && value < LeadBase + LeadCount;
        }

        bool IsVowel(char32_t value)
        {
            return value >= VowelBase && value < VowelBase + VowelCount;
        }

        bool IsTrail(char32_t value)
        {
            return value > TrailBase && value < TrailBase + TrailCount;
        }

        bool IsSyllableWithoutTrail(char32_t value)
        {
            return value >= HangulBase && value < HangulBase + SyllableCount && (value - HangulBase) % TrailCount == 0;
        }

        bool IsSpace(char32_t value)
        {
            return value == 0x20 || value == 0x09 || value == 0x3000;
        }

        // 띄어 쓰지 않는 문자 체계다. Word 모드에서도 이 글자의 앞뒤에서 줄을 바꿀 수 있다.
        bool IsBreakAnywhereScript(char32_t value)
        {
            return (value >= 0x3040 && value <= 0x30FF)     // 히라가나·가타카나
                || (value >= 0x3400 && value <= 0x4DBF)     // CJK 확장 A
                || (value >= 0x4E00 && value <= 0x9FFF)     // CJK 통합 한자
                || (value >= 0xF900 && value <= 0xFAFF)     // CJK 호환 한자
                || (value >= 0x20000 && value <= 0x2FFFF);  // CJK 확장 B 이후
        }

        struct FaceChoice
        {
            GlyphIndex    glyph = MissingGlyph;
            std::uint16_t face = 0;
        };

        // 앞에서부터 그 글자가 있는 face 를 고른다. 어디에도 없으면 U+FFFD 를, 그것도 없으면 첫 face 의 .notdef 를 쓴다.
        FaceChoice ChooseFace(ArrayView<const FontFace* const> faces, std::uint16_t primary, char32_t codepoint)
        {
            for (std::size_t index = 0; index < faces.Size(); ++index)
            {
                const FontFace* face = faces[index];
                if (face == nullptr || false == face->IsLoaded())
                {
                    continue;
                }
                const GlyphIndex glyph = face->FindGlyph(codepoint);
                if (glyph != MissingGlyph)
                {
                    return { glyph, static_cast<std::uint16_t>(index) };
                }
            }
            if (codepoint != ReplacementCharacter)
            {
                const FaceChoice replacement = ChooseFace(faces, primary, ReplacementCharacter);
                if (replacement.glyph != MissingGlyph)
                {
                    return replacement;
                }
            }
            return { MissingGlyph, primary };
        }

        float Scale(const FontFace& face, float fontSize)
        {
            return fontSize / static_cast<float>(face.GetMetrics().unitsPerEm);
        }
    }

    void TextLayout::Reset()
    {
        m_codepoints.Clear();
        m_items.Clear();
        m_glyphs.Clear();
        m_lines.Clear();
        m_minX = 0.0f;
        m_minY = 0.0f;
        m_maxX = 0.0f;
        m_maxY = 0.0f;
    }

    LayoutError TextLayout::Build(ArrayView<const char> utf8, ArrayView<const FontFace* const> faces, const LayoutOptions& options)
    {
        Reset();

        const FontFace* primaryFace = nullptr;
        std::uint16_t primary = 0;
        for (std::size_t index = 0; index < faces.Size(); ++index)
        {
            if (faces[index] != nullptr && faces[index]->IsLoaded())
            {
                primaryFace = faces[index];
                primary = static_cast<std::uint16_t>(index);
                break;
            }
        }
        if (primaryFace == nullptr || faces.Size() > std::numeric_limits<std::uint16_t>::max())
        {
            return LayoutError::NoFace;
        }
        if (false == (options.fontSize > 0.0f) || false == std::isfinite(options.fontSize))
        {
            return LayoutError::InvalidFontSize;
        }
        if (utf8.Size() > std::numeric_limits<std::uint32_t>::max())
        {
            return LayoutError::TooLong;
        }

        // 1. 코드포인트로 푼다. CR 과 CRLF 는 LF 하나다.
        const char* text = utf8.Data();
        const std::size_t length = utf8.Size();
        std::size_t cursor = 0;
        while (cursor < length)
        {
            const std::uint32_t offset = static_cast<std::uint32_t>(cursor);
            char32_t value = DecodeOne(text, length, cursor);
            if (value == U'\r')
            {
                if (cursor < length && text[cursor] == '\n')
                {
                    ++cursor;
                }
                value = U'\n';
            }
            m_codepoints.Add({ value, offset });
        }

        // 2. 자모를 음절로 합치고, 글자마다 face·글리프·전진 폭·줄바꿈 성질을 정한다.
        for (std::size_t index = 0; index < m_codepoints.Size(); ++index)
        {
            char32_t value = m_codepoints[index].value;
            const std::uint32_t offset = m_codepoints[index].offset;
            if (IsLead(value) && index + 1 < m_codepoints.Size() && IsVowel(m_codepoints[index + 1].value))
            {
                value = HangulBase + ((value - LeadBase) * VowelCount + (m_codepoints[index + 1].value - VowelBase)) * TrailCount;
                ++index;
            }
            if (IsSyllableWithoutTrail(value) && index + 1 < m_codepoints.Size() && IsTrail(m_codepoints[index + 1].value))
            {
                value += m_codepoints[index + 1].value - TrailBase;
                ++index;
            }

            Item item;
            item.codepoint = value;
            item.offset = offset;
            if (value == U'\n')
            {
                item.kind = ItemKind::Newline;
                m_items.Add(item);
                continue;
            }
            item.kind = IsSpace(value) ? ItemKind::Space : ItemKind::Visible;
            // 탭은 1 판에서 공백 하나다(탭 멈춤 자리 없음, text-plan §7).
            const char32_t lookup = value == 0x09 ? U' ' : value;
            const FaceChoice choice = ChooseFace(faces, primary, lookup);
            item.glyph = choice.glyph;
            item.face = choice.face;
            item.breaksAnywhere = item.kind == ItemKind::Visible
                && (options.wrapMode == WrapMode::Character || IsBreakAnywhereScript(value));
            const FontFace& face = *faces[choice.face];
            item.advance = static_cast<float>(face.GetAdvance(choice.glyph)) * Scale(face, options.fontSize);
            m_items.Add(item);
        }

        // 3. 줄을 나눈다. 폭을 넘으면 그 줄의 마지막 기회로 돌아가 거기서부터 새 줄을 다시 매긴다.
        const bool wraps = options.overflow != Overflow::Overflow && options.boxWidth > 0.0f;
        const float wrapLimit = options.boxWidth + options.boxWidth * 1.0e-5f;
        const float primaryScale = Scale(*primaryFace, options.fontSize);
        const FontMetrics& metrics = primaryFace->GetMetrics();
        const float ascent = static_cast<float>(metrics.ascent) * primaryScale;
        const float descent = static_cast<float>(-metrics.descent) * primaryScale;
        const float lineHeight = static_cast<float>(metrics.ascent - metrics.descent + metrics.lineGap)
            * primaryScale * std::max(0.0f, options.lineSpacing);

        // 줄 끝을 매긴다: [begin, end) 의 글자에서 보이는 것만 글리프로 내고, 끝 공백을 뺀 폭을 잰다.
        auto finishLine = [&](std::size_t begin, std::size_t end) -> bool
        {
            if (m_lines.Size() >= std::numeric_limits<std::uint16_t>::max())
            {
                return false;
            }
            LineInfo line;
            line.firstGlyph = static_cast<std::uint32_t>(m_glyphs.Size());
            line.sourceBegin = begin < m_items.Size() ? m_items[begin].offset : static_cast<std::uint32_t>(length);
            line.sourceEnd = end < m_items.Size() ? m_items[end].offset : static_cast<std::uint32_t>(length);
            for (std::size_t index = begin; index < end; ++index)
            {
                const Item& item = m_items[index];
                if (item.kind != ItemKind::Visible)
                {
                    continue;
                }
                PositionedGlyph glyph;
                glyph.x = item.x;
                glyph.glyph = item.glyph;
                glyph.face = item.face;
                glyph.line = static_cast<std::uint16_t>(m_lines.Size());
                glyph.sourceOffset = item.offset;
                m_glyphs.Add(glyph);
                line.width = std::max(line.width, item.x + item.advance);
            }
            line.glyphCount = static_cast<std::uint32_t>(m_glyphs.Size()) - line.firstGlyph;
            m_lines.Add(line);
            return true;
        };

        std::size_t lineStart = 0;
        std::size_t index = 0;
        float penX = 0.0f;
        std::size_t lastOpportunity = 0;
        while (index < m_items.Size())
        {
            Item& item = m_items[index];
            if (item.kind == ItemKind::Newline)
            {
                if (false == finishLine(lineStart, index))
                {
                    Reset();
                    return LayoutError::TooLong;
                }
                lineStart = index + 1;
                lastOpportunity = lineStart;
                penX = 0.0f;
                ++index;
                continue;
            }

            float x = penX;
            if (index > lineStart)
            {
                const Item& previous = m_items[index - 1];
                if (previous.face == item.face)
                {
                    const FontFace& face = *faces[item.face];
                    x += static_cast<float>(face.GetKerning(previous.glyph, item.glyph)) * Scale(face, options.fontSize);
                }
                // 공백은 줄 끝에 매달리므로 새 줄은 공백이 아닌 글자에서만 시작한다.
                const bool opportunity = item.kind == ItemKind::Visible
                    && (previous.kind == ItemKind::Space || previous.breaksAnywhere || item.breaksAnywhere);
                if (opportunity)
                {
                    lastOpportunity = index;
                }
            }

            if (wraps && item.kind == ItemKind::Visible && index > lineStart && x + item.advance > wrapLimit)
            {
                const std::size_t breakAt = lastOpportunity > lineStart ? lastOpportunity : index;
                if (false == finishLine(lineStart, breakAt))
                {
                    Reset();
                    return LayoutError::TooLong;
                }
                lineStart = breakAt;
                lastOpportunity = breakAt;
                index = breakAt;
                penX = 0.0f;
                continue;
            }

            item.x = x;
            penX = x + item.advance + options.letterSpacing;
            ++index;
        }
        if (m_items.Size() > 0)
        {
            if (false == finishLine(lineStart, m_items.Size()))
            {
                Reset();
                return LayoutError::TooLong;
            }
        }

        // 4. 정렬한다. 블록을 정하고, 줄을 블록 안에 붙이고, 기준점을 원점으로 옮긴다.
        float widest = 0.0f;
        for (const LineInfo& line : m_lines)
        {
            widest = std::max(widest, line.width);
        }
        const float blockWidth = options.boxWidth > 0.0f ? options.boxWidth : widest;
        const float contentHeight = static_cast<float>(m_lines.Size()) * lineHeight;
        const float blockHeight = options.boxHeight > 0.0f ? options.boxHeight : contentHeight;

        float blockLeft = 0.0f;
        if (options.alignX == AlignX::Center)
        {
            blockLeft = -blockWidth * 0.5f;
        }
        else if (options.alignX == AlignX::Right)
        {
            blockLeft = -blockWidth;
        }
        float blockTop = 0.0f;
        if (options.alignY == AlignY::Middle)
        {
            blockTop = blockHeight * 0.5f;
        }
        else if (options.alignY == AlignY::Bottom)
        {
            blockTop = blockHeight;
        }
        else if (options.alignY == AlignY::Baseline)
        {
            blockTop = ascent;
        }

        // Clip 은 **위쪽이 상자 밖에 있는** 줄을 통째로 버린다. 걸쳐 있는 줄은 남기고, 상자 밖으로 나간 글리프 조각은
        // 그리는 쪽(Text2DSystem)이 사각형과 UV 를 줄여 잘라 낸다 - 상자보다 큰 글자 한 줄도 가려진 채 보여야 한다.
        std::size_t keptLines = m_lines.Size();
        if (options.overflow == Overflow::Clip && options.boxHeight > 0.0f)
        {
            keptLines = 0;
            while (keptLines < m_lines.Size()
                && static_cast<float>(keptLines) * lineHeight < options.boxHeight * (1.0f - 1.0e-5f))
            {
                ++keptLines;
            }
        }

        for (std::size_t lineIndex = 0; lineIndex < m_lines.Size(); ++lineIndex)
        {
            LineInfo& line = m_lines[lineIndex];
            line.baseline = blockTop - ascent - static_cast<float>(lineIndex) * lineHeight;
            float shift = blockLeft;
            if (options.alignX == AlignX::Center)
            {
                shift += (blockWidth - line.width) * 0.5f;
            }
            else if (options.alignX == AlignX::Right)
            {
                shift += blockWidth - line.width;
            }
            for (std::uint32_t glyphIndex = 0; glyphIndex < line.glyphCount; ++glyphIndex)
            {
                PositionedGlyph& glyph = m_glyphs[line.firstGlyph + glyphIndex];
                glyph.x += shift;
                glyph.y = line.baseline;
            }
        }
        if (keptLines < m_lines.Size())
        {
            const std::size_t keptGlyphs = keptLines > 0
                ? m_lines[keptLines - 1].firstGlyph + m_lines[keptLines - 1].glyphCount
                : 0;
            m_glyphs.Resize(keptGlyphs);
            m_lines.Resize(keptLines);
        }

        m_minX = blockLeft;
        m_maxX = blockLeft + blockWidth;
        m_maxY = blockTop;
        m_minY = blockTop - blockHeight;
        return LayoutError::None;
    }

    ArrayView<const PositionedGlyph> TextLayout::GetGlyphs() const
    {
        return ArrayView<const PositionedGlyph>(m_glyphs.Data(), m_glyphs.Size());
    }

    ArrayView<const LineInfo> TextLayout::GetLines() const
    {
        return ArrayView<const LineInfo>(m_lines.Data(), m_lines.Size());
    }

    float TextLayout::GetMinX() const
    {
        return m_minX;
    }

    float TextLayout::GetMinY() const
    {
        return m_minY;
    }

    float TextLayout::GetMaxX() const
    {
        return m_maxX;
    }

    float TextLayout::GetMaxY() const
    {
        return m_maxY;
    }

    std::size_t TextLayout::GetReservedCapacity() const
    {
        return m_codepoints.Capacity() + m_items.Capacity() + m_glyphs.Capacity() + m_lines.Capacity();
    }
}
