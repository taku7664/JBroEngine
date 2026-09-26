#include <JBro/Text/TextLayout.h>

#include <algorithm>
#include <charconv>
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

        // **줄 머리 금칙**: 이 글자로 줄을 시작하지 않는다(닫는 괄호·마침표류·일본어 작은 가나와 장음). 그 앞의 줄바꿈 기회를 버린다.
        bool IsNoLineStart(char32_t value)
        {
            switch (value)
            {
            case U')': case U']': case U'}': case U',': case U'.': case U'!': case U'?': case U':': case U';':
            case U'%': case 0x2019: case 0x201D: case 0x2026: case 0x3001: case 0x3002: case 0x3009: case 0x300B:
            case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x30FC: case 0xFF09: case 0xFF0C: case 0xFF0E:
            case 0xFF1A: case 0xFF1B: case 0xFF01: case 0xFF1F: case 0xFF5D: case 0xFF3D:
                return true;
            default:
                break;
            }
            // 작은 가나(ぁぃぅぇぉっゃゅょゎ, 가타카나 같은 자리)
            switch (value)
            {
            case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085:
            case 0x3087: case 0x308E: case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3:
            case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
                return true;
            default:
                return false;
            }
        }

        // **줄 꼬리 금칙**: 이 글자로 줄을 끝내지 않는다(여는 괄호·여는 따옴표). 그 뒤의 줄바꿈 기회를 버린다.
        bool IsNoLineEnd(char32_t value)
        {
            switch (value)
            {
            case U'(': case U'[': case U'{': case 0x2018: case 0x201C: case 0x3008: case 0x300A: case 0x300C:
            case 0x300E: case 0x3010: case 0x3014: case 0xFF08: case 0xFF3B: case 0xFF5B:
                return true;
            default:
                return false;
            }
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

        // 앞 글자에 붙는 결합 표시다(결합 분음 부호와 그 보충·확장, 기호용 결합 표시, 결합 반쪽 표시).
        bool IsCombiningMark(char32_t value)
        {
            return (value >= 0x0300 && value <= 0x036F)
                || (value >= 0x1AB0 && value <= 0x1AFF)
                || (value >= 0x1DC0 && value <= 0x1DFF)
                || (value >= 0x20D0 && value <= 0x20FF)
                || (value >= 0xFE20 && value <= 0xFE2F);
        }

        struct FaceChoice
        {
            GlyphIndex    glyph = MissingGlyph;
            std::uint16_t face = 0;
        };

        // 스타일 글자는 그 스타일의 face 를 먼저 본다(D-225). 굵은 기울임이 없으면 굵게, 기울임 순이다. 거기 글자가 없으면 보통 고르기다.
        std::uint16_t StyleFaceOf(const LayoutOptions& options, std::uint8_t style, std::size_t faceCount)
        {
            const auto valid = [&](std::uint16_t face) { return face != LayoutOptions::NoStyleFace && face < faceCount; };
            if ((style & GlyphStyleBold) != 0 && (style & GlyphStyleItalic) != 0 && valid(options.boldItalicFace))
            {
                return options.boldItalicFace;
            }
            if ((style & GlyphStyleBold) != 0 && valid(options.boldFace))
            {
                return options.boldFace;
            }
            if ((style & GlyphStyleItalic) != 0 && valid(options.italicFace))
            {
                return options.italicFace;
            }
            return LayoutOptions::NoStyleFace;
        }

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

        // 리치 텍스트 태그의 겹침 상한이다. 넘는 여는 태그는 글자로 보인다.
        constexpr int MaxMarkupDepth = 8;
        // 태그 하나의 최대 길이(꺾쇠 안쪽)다. `color=#RRGGBBAA` 가 14 바이트다.
        constexpr std::size_t MaxTagLength = 24;

        bool Matches(const char* text, std::size_t length, const char* word)
        {
            std::size_t index = 0;
            for (; word[index] != 0; ++index)
            {
                if (index >= length || text[index] != word[index])
                {
                    return false;
                }
            }
            return index == length;
        }

        bool StartsWith(const char* text, std::size_t length, const char* word)
        {
            std::size_t index = 0;
            for (; word[index] != 0; ++index)
            {
                if (index >= length || text[index] != word[index])
                {
                    return false;
                }
            }
            return true;
        }

        int HexDigit(char value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        }

        // `RRGGBB` 또는 `RRGGBBAA` 를 RGBA8(R 이 가장 낮은 바이트)로 읽는다. 알파가 없으면 255 다.
        bool ParseHexColor(const char* text, std::size_t length, std::uint32_t& color)
        {
            if (length != 6 && length != 8)
            {
                return false;
            }
            std::uint32_t channels[4] = { 0, 0, 0, 255 };
            for (std::size_t channel = 0; channel < length / 2; ++channel)
            {
                const int high = HexDigit(text[channel * 2]);
                const int low = HexDigit(text[channel * 2 + 1]);
                if (high < 0 || low < 0)
                {
                    return false;
                }
                channels[channel] = static_cast<std::uint32_t>(high * 16 + low);
            }
            color = channels[0] | (channels[1] << 8) | (channels[2] << 16) | (channels[3] << 24);
            return true;
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
        m_contentWidth = 0.0f;
        m_contentHeight = 0.0f;
        m_forcedBreaks = 0;
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

        // 1. 코드포인트로 푼다. CR 과 CRLF 는 LF 하나다. 리치 텍스트면 태그를 읽어 글자마다 크기와 색을 붙인다.
        const char* text = utf8.Data();
        const std::size_t length = utf8.Size();
        const float markupScale = std::isfinite(options.markupScale) && options.markupScale > 0.0f ? options.markupScale : 1.0f;
        float sizeStack[MaxMarkupDepth] = {};
        std::uint32_t colorStack[MaxMarkupDepth] = {};
        int sizeDepth = 0;
        int colorDepth = 0;
        int boldDepth = 0;
        int italicDepth = 0;
        const auto currentSize = [&]() { return sizeDepth > 0 ? sizeStack[sizeDepth - 1] : options.fontSize; };
        const auto add = [&](char32_t value, std::uint32_t offset) {
            Codepoint codepoint;
            codepoint.value = value;
            codepoint.offset = offset;
            codepoint.size = currentSize();
            codepoint.hasColor = colorDepth > 0;
            codepoint.color = colorDepth > 0 ? colorStack[colorDepth - 1] : 0;
            codepoint.style = static_cast<std::uint8_t>((boldDepth > 0 ? GlyphStyleBold : 0) | (italicDepth > 0 ? GlyphStyleItalic : 0));
            m_codepoints.Add(codepoint);
        };
        // 꺾쇠 하나를 태그로 읽는다. 태그면 스택을 바꾸고 커서를 `>` 뒤로 옮긴다. 아니면 아무것도 바꾸지 않는다.
        const auto readTag = [&](std::size_t& at) -> bool {
            std::size_t close = at + 1;
            while (close < length && close - at - 1 <= MaxTagLength && text[close] != '>' && text[close] != '<')
            {
                ++close;
            }
            if (close >= length || text[close] != '>')
            {
                return false;
            }
            const char* body = text + at + 1;
            const std::size_t bodyLength = close - at - 1;
            if (Matches(body, bodyLength, "b") || Matches(body, bodyLength, "i"))
            {
                int& depth = body[0] == 'b' ? boldDepth : italicDepth;
                if (depth >= MaxMarkupDepth)
                {
                    return false;
                }
                ++depth;
            }
            else if (Matches(body, bodyLength, "/b") || Matches(body, bodyLength, "/i"))
            {
                int& depth = body[1] == 'b' ? boldDepth : italicDepth;
                if (depth == 0)
                {
                    return false;
                }
                --depth;
            }
            else if (Matches(body, bodyLength, "/color"))
            {
                if (colorDepth == 0)
                {
                    return false;
                }
                --colorDepth;
            }
            else if (Matches(body, bodyLength, "/size"))
            {
                if (sizeDepth == 0)
                {
                    return false;
                }
                --sizeDepth;
            }
            else if (StartsWith(body, bodyLength, "color=#"))
            {
                std::uint32_t color = 0;
                if (colorDepth >= MaxMarkupDepth || false == ParseHexColor(body + 7, bodyLength - 7, color))
                {
                    return false;
                }
                colorStack[colorDepth++] = color;
            }
            else if (StartsWith(body, bodyLength, "size="))
            {
                float size = 0.0f;
                const std::from_chars_result parsed = std::from_chars(body + 5, body + bodyLength, size);
                if (sizeDepth >= MaxMarkupDepth || parsed.ec != std::errc() || parsed.ptr != body + bodyLength
                    || false == std::isfinite(size) || false == (size > 0.0f))
                {
                    return false;
                }
                size *= markupScale;
                if (options.wholePixelMarkup)
                {
                    size = std::max(1.0f, std::round(size));
                }
                sizeStack[sizeDepth++] = size;
            }
            else
            {
                return false;
            }
            at = close + 1;
            return true;
        };
        std::size_t cursor = 0;
        while (cursor < length)
        {
            const std::uint32_t offset = static_cast<std::uint32_t>(cursor);
            if (options.richText && text[cursor] == '<')
            {
                if (cursor + 1 < length && text[cursor + 1] == '<')
                {
                    add(U'<', offset);
                    cursor += 2;
                    continue;
                }
                if (readTag(cursor))
                {
                    continue;
                }
            }
            char32_t value = DecodeOne(text, length, cursor);
            if (value == U'\r')
            {
                if (cursor < length && text[cursor] == '\n')
                {
                    ++cursor;
                }
                value = U'\n';
            }
            add(value, offset);
        }

        // 2. 자모를 음절로 합치고, 글자마다 face·글리프·전진 폭·줄바꿈 성질을 정한다.
        for (std::size_t index = 0; index < m_codepoints.Size(); ++index)
        {
            char32_t value = m_codepoints[index].value;
            const std::uint32_t offset = m_codepoints[index].offset;
            const std::size_t first = index;
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
            item.size = m_codepoints[first].size;
            item.color = m_codepoints[first].color;
            item.hasColor = m_codepoints[first].hasColor;
            item.style = m_codepoints[first].style;
            if (value == U'\n')
            {
                item.kind = ItemKind::Newline;
                m_items.Add(item);
                continue;
            }
            // 결합 표시는 바로 앞의 보이는 글자(또는 같은 글자에 먼저 붙은 표시)에 붙는다. 받침이 없으면(줄 머리·공백 뒤) 보통 글자다.
            if (IsCombiningMark(value) && m_items.Size() > 0
                && (m_items.Last().kind == ItemKind::Visible || m_items.Last().kind == ItemKind::Mark))
            {
                const std::uint32_t baseIndex = m_items.Last().kind == ItemKind::Mark
                    ? m_items.Last().markBase
                    : static_cast<std::uint32_t>(m_items.Size() - 1);
                const Item& base = m_items[baseIndex];
                item.kind = ItemKind::Mark;
                item.markBase = baseIndex;
                item.breaksAnywhere = base.breaksAnywhere;
                // 앵커는 받침과 같은 폰트 안에만 있으므로, 받침의 폰트에 표시가 있으면 그 폰트로 그린다.
                const FontFace& baseFace = *faces[base.face];
                const GlyphIndex sameFace = baseFace.FindGlyph(value);
                FaceChoice choice;
                if (sameFace != MissingGlyph)
                {
                    choice.glyph = sameFace;
                    choice.face = base.face;
                }
                else
                {
                    choice = ChooseFace(faces, primary, value);
                }
                item.glyph = choice.glyph;
                item.face = choice.face;
                std::int32_t dx = 0;
                std::int32_t dy = 0;
                if (choice.face == base.face && baseFace.GetMarkAttachment(base.glyph, choice.glyph, dx, dy))
                {
                    const float scale = Scale(baseFace, base.size);
                    item.markX = static_cast<float>(dx) * scale;
                    item.markY = static_cast<float>(dy) * scale;
                }
                else
                {
                    item.markX = base.advance;
                }
                m_items.Add(item);
                continue;
            }
            item.kind = IsSpace(value) ? ItemKind::Space : ItemKind::Visible;
            // 탭은 공백 글리프로 재고, 줄을 나눌 때 멈춤 자리까지 폭을 늘린다(아래 3.).
            const char32_t lookup = value == 0x09 ? U' ' : value;
            FaceChoice choice;
            const std::uint16_t styleFace = item.style != 0 ? StyleFaceOf(options, item.style, faces.Size()) : LayoutOptions::NoStyleFace;
            const GlyphIndex styled = styleFace != LayoutOptions::NoStyleFace && faces[styleFace] != nullptr && faces[styleFace]->IsLoaded()
                ? faces[styleFace]->FindGlyph(lookup)
                : MissingGlyph;
            if (styled != MissingGlyph)
            {
                choice.glyph = styled;
                choice.face = styleFace;
            }
            else
            {
                choice = ChooseFace(faces, primary, lookup);
            }
            item.glyph = choice.glyph;
            item.face = choice.face;
            item.breaksAnywhere = item.kind == ItemKind::Visible
                && (options.wrapMode == WrapMode::Character || IsBreakAnywhereScript(value));
            const FontFace& face = *faces[choice.face];
            item.advance = static_cast<float>(face.GetAdvance(choice.glyph)) * Scale(face, item.size);
            m_items.Add(item);
        }

        // 3. 줄을 나눈다. 폭을 넘으면 그 줄의 마지막 기회로 돌아가 거기서부터 새 줄을 다시 매긴다.
        const bool wraps = options.overflow != Overflow::Overflow && options.boxWidth > 0.0f;
        const float wrapLimit = options.boxWidth + options.boxWidth * 1.0e-5f;
        const float primaryScale = Scale(*primaryFace, options.fontSize);
        const FontMetrics& metrics = primaryFace->GetMetrics();
        // 줄의 올림과 높이는 그 줄에서 가장 큰 글자의 크기로 잰다. 리치 텍스트가 아니면 모든 줄이 fontSize 다.
        const auto ascentOf = [&](float size) { return static_cast<float>(metrics.ascent) * Scale(*primaryFace, size); };
        const auto lineHeightOf = [&](float size) {
            return static_cast<float>(metrics.ascent - metrics.descent + metrics.lineGap) * Scale(*primaryFace, size)
                * std::max(0.0f, options.lineSpacing);
        };
        // 탭 멈춤 간격(픽셀)이다. 기본 폰트의 공백 폭으로 센다 - 폴백 폰트가 섞여도 멈춤 자리는 한 줄 안에서 같다.
        const float tabStop = options.tabSize > 0.0f && std::isfinite(options.tabSize)
            ? static_cast<float>(primaryFace->GetAdvance(primaryFace->FindGlyph(U' '))) * primaryScale * options.tabSize
            : 0.0f;

        // 줄 끝을 매긴다: [begin, end) 의 글자에서 보이는 것만 글리프로 내고, 끝 공백을 뺀 폭을 잰다.
        auto finishLine = [&](std::size_t begin, std::size_t end) -> bool
        {
            if (m_lines.Size() >= std::numeric_limits<std::uint16_t>::max())
            {
                return false;
            }
            LineInfo line;
            // 빈 줄은 그 자리(개행 글자)의 크기다. 끝의 빈 줄은 마지막 글자의 크기다.
            line.size = begin < m_items.Size() ? m_items[begin].size : (m_items.Size() > 0 ? m_items.Last().size : options.fontSize);
            for (std::size_t index = begin; index < end; ++index)
            {
                line.size = std::max(line.size, m_items[index].size);
            }
            line.height = lineHeightOf(line.size);
            line.firstGlyph = static_cast<std::uint32_t>(m_glyphs.Size());
            line.sourceBegin = begin < m_items.Size() ? m_items[begin].offset : static_cast<std::uint32_t>(length);
            line.sourceEnd = end < m_items.Size() ? m_items[end].offset : static_cast<std::uint32_t>(length);
            for (std::size_t index = begin; index < end; ++index)
            {
                const Item& item = m_items[index];
                if (item.kind != ItemKind::Visible && item.kind != ItemKind::Mark)
                {
                    continue;
                }
                PositionedGlyph glyph;
                glyph.x = item.x;
                glyph.y = item.markY; // 아래 4. 에서 기준선을 더한다
                glyph.glyph = item.glyph;
                glyph.face = item.face;
                glyph.line = static_cast<std::uint16_t>(m_lines.Size());
                glyph.sourceOffset = item.offset;
                glyph.size = item.size;
                glyph.color = item.color;
                glyph.hasColor = item.hasColor;
                glyph.style = item.style;
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

            if (item.kind == ItemKind::Mark)
            {
                // 받침은 늘 앞에 있고(같은 줄), 이번 줄 매기기에서 이미 자리를 받았다. 펜은 움직이지 않는다.
                item.x = m_items[item.markBase].x + item.markX;
                ++index;
                continue;
            }

            float x = penX;
            if (index > lineStart)
            {
                // 표시 뒤의 글자는 표시가 아니라 그 받침과 짝을 짓는다(커닝·금칙).
                const Item& previous = m_items[index - 1].kind == ItemKind::Mark
                    ? m_items[m_items[index - 1].markBase]
                    : m_items[index - 1];
                if (previous.face == item.face)
                {
                    const FontFace& face = *faces[item.face];
                    x += static_cast<float>(face.GetKerning(previous.glyph, item.glyph)) * Scale(face, item.size);
                }
                // 공백은 줄 끝에 매달리므로 새 줄은 공백이 아닌 글자에서만 시작한다. 금칙 글자는 줄 머리·꼬리에 오지 않게 기회를 버린다
                // (기회가 없는 줄은 여전히 넘친 글자에서 끊는다 - 금칙은 끊을 자리가 있을 때만 지켜진다).
                const bool opportunity = item.kind == ItemKind::Visible
                    && (previous.kind == ItemKind::Space || previous.breaksAnywhere || item.breaksAnywhere)
                    && false == IsNoLineStart(item.codepoint)
                    && false == IsNoLineEnd(previous.codepoint);
                if (opportunity)
                {
                    lastOpportunity = index;
                }
            }

            if (wraps && item.kind == ItemKind::Visible && index > lineStart && x + item.advance > wrapLimit)
            {
                const std::size_t breakAt = lastOpportunity > lineStart ? lastOpportunity : index;
                if (breakAt == index && false == item.breaksAnywhere)
                {
                    ++m_forcedBreaks;
                }
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
            if (item.codepoint == 0x09 && tabStop > 0.0f)
            {
                // 다음 멈춤 자리까지 나아간다. 멈춤 자리에 딱 있으면 그다음 자리다.
                const float next = (std::floor(x / tabStop + 1.0e-4f) + 1.0f) * tabStop;
                item.advance = next - x;
            }
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
        float contentHeight = 0.0f;
        for (const LineInfo& line : m_lines)
        {
            contentHeight += line.height;
        }
        m_contentWidth = widest;
        m_contentHeight = contentHeight;
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
            blockTop = ascentOf(m_lines.Size() > 0 ? m_lines[0].size : options.fontSize);
        }

        // Clip 은 **위쪽이 상자 밖에 있는** 줄을 통째로 버린다. 걸쳐 있는 줄은 남기고, 상자 밖으로 나간 글리프 조각은
        // 그리는 쪽(Text2DSystem)이 사각형과 UV 를 줄여 잘라 낸다 - 상자보다 큰 글자 한 줄도 가려진 채 보여야 한다.
        std::size_t keptLines = m_lines.Size();
        if (options.overflow == Overflow::Clip && options.boxHeight > 0.0f)
        {
            keptLines = 0;
            float lineTop = 0.0f;
            while (keptLines < m_lines.Size() && lineTop < options.boxHeight * (1.0f - 1.0e-5f))
            {
                lineTop += m_lines[keptLines].height;
                ++keptLines;
            }
        }

        float linesAbove = 0.0f;
        for (std::size_t lineIndex = 0; lineIndex < m_lines.Size(); ++lineIndex)
        {
            LineInfo& line = m_lines[lineIndex];
            line.baseline = blockTop - ascentOf(line.size) - linesAbove;
            linesAbove += line.height;
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
                glyph.y += line.baseline;
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

    LayoutError TextLayout::BuildToFit(ArrayView<const char> utf8, ArrayView<const FontFace* const> faces,
        const LayoutOptions& options, float minSize, float maxSize, float step, float& chosenSize)
    {
        if (false == std::isfinite(minSize) || false == std::isfinite(maxSize) || false == (minSize > 0.0f))
        {
            return LayoutError::InvalidFontSize;
        }
        if (maxSize < minSize)
        {
            maxSize = minSize;
        }
        LayoutOptions trial = options;
        const auto fits = [&]() {
            const float widthLimit = options.boxWidth * (1.0f + 1.0e-5f);
            const float heightLimit = options.boxHeight * (1.0f + 1.0e-5f);
            if (options.boxWidth > 0.0f && m_contentWidth > widthLimit)
            {
                return false;
            }
            if (options.boxHeight > 0.0f && m_contentHeight > heightLimit)
            {
                return false;
            }
            return options.wrapMode != WrapMode::Word || m_forcedBreaks == 0;
        };
        const auto buildAt = [&](float size) {
            trial.fontSize = size;
            // 태그 크기도 같은 비로 줄고 는다.
            trial.markupScale = options.markupScale * size / options.fontSize;
            return Build(utf8, faces, trial);
        };
        // 크기를 격자로 센다. step 이 1 이면 정수, 0 이면 0.25 픽셀 칸이다. 안쪽 끝은 칸에 맞춰 줄인다.
        const float cell = step > 0.0f ? step : 0.25f;
        const std::int64_t low = static_cast<std::int64_t>(std::ceil(minSize / cell));
        const std::int64_t high = std::max(low, static_cast<std::int64_t>(std::floor(maxSize / cell)));
        LayoutError error = buildAt(static_cast<float>(high) * cell);
        if (error != LayoutError::None)
        {
            return error;
        }
        if (fits())
        {
            chosenSize = static_cast<float>(high) * cell;
            return LayoutError::None;
        }
        // [best, bad) 사이를 좁힌다. best 는 들어가는 것이 확인된 가장 큰 칸이다(없으면 low 로 넘친다).
        std::int64_t best = low;
        std::int64_t bad = high;
        bool lowFits = false;
        error = buildAt(static_cast<float>(low) * cell);
        if (error != LayoutError::None)
        {
            return error;
        }
        lowFits = fits();
        if (lowFits)
        {
            while (bad - best > 1)
            {
                const std::int64_t middle = best + (bad - best) / 2;
                error = buildAt(static_cast<float>(middle) * cell);
                if (error != LayoutError::None)
                {
                    return error;
                }
                if (fits())
                {
                    best = middle;
                }
                else
                {
                    bad = middle;
                }
            }
        }
        chosenSize = static_cast<float>(best) * cell;
        return buildAt(chosenSize);
    }

    float TextLayout::GetContentWidth() const
    {
        return m_contentWidth;
    }

    float TextLayout::GetContentHeight() const
    {
        return m_contentHeight;
    }

    std::uint32_t TextLayout::GetForcedBreakCount() const
    {
        return m_forcedBreaks;
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
