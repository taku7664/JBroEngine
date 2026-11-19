#include <JBro/Asset/ColorLutFile.h>

#include <JBro/Types/Int.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace JBro
{
    namespace
    {
        void Fail(String* error, UInt32 line, const char* reason)
        {
            if (error != nullptr)
            {
                *error = "line ";
                error->append(std::to_string(line.Get()));
                error->append(": ");
                error->append(reason);
            }
        }

        Bool IsSpace(char value)
        {
            return value == ' ' || value == '\t' || value == '\r';
        }

        // 공백으로 나뉜 실수 `count` 개를 읽는다. 더 있으면 거짓이다.
        Bool ReadFloats(const char* begin, const char* end, Float* out, UInt32 count)
        {
            const char* at = begin;
            for (UInt32 index = 0; index < count; ++index)
            {
                while (at < end && IsSpace(*at))
                {
                    ++at;
                }
                if (at >= end)
                {
                    return false;
                }
                char buffer[64] = {};
                std::size_t length = 0;
                while (at < end && false == IsSpace(*at) && length + 1 < sizeof(buffer))
                {
                    buffer[length++] = *at++;
                }
                char* parsed = nullptr;
                const double value = std::strtod(buffer, &parsed);
                if (parsed == buffer || *parsed != '\0' || false == std::isfinite(value))
                {
                    return false;
                }
                out[index.Get()] = static_cast<JBro::Float>(value);
            }
            while (at < end && IsSpace(*at))
            {
                ++at;
            }
            return at == end;
        }

        Bool StartsWith(const char* begin, const char* end, const char* keyword)
        {
            const std::size_t length = std::strlen(keyword);
            return static_cast<std::size_t>(end - begin) > length && std::memcmp(begin, keyword, length) == 0 && IsSpace(begin[length]);
        }

        // IEEE 754 반정밀로 옮긴다(가장 가까운 짝수로 반올림). 범위를 넘으면 무한대다.
        std::uint16_t ToHalf(Float value)
        {
            const float raw = value.Get();
            std::uint32_t bits = 0;
            std::memcpy(&bits, &raw, sizeof(bits));
            const std::uint32_t sign = (bits >> 16) & 0x8000u;
            const std::uint32_t exponent = (bits >> 23) & 0xFFu;
            std::uint32_t mantissa = bits & 0x7FFFFFu;
            if (exponent == 0xFFu)
            {
                return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u));
            }
            const std::int32_t halfExponent = static_cast<std::int32_t>(exponent) - 127 + 15;
            if (halfExponent >= 31)
            {
                return static_cast<std::uint16_t>(sign | 0x7C00u);
            }
            if (halfExponent <= 0)
            {
                if (halfExponent < -10)
                {
                    return static_cast<std::uint16_t>(sign);
                }
                mantissa |= 0x800000u;
                const std::uint32_t shift = static_cast<std::uint32_t>(14 - halfExponent);
                std::uint32_t half = mantissa >> shift;
                const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
                const std::uint32_t halfway = 1u << (shift - 1u);
                if (remainder > halfway || (remainder == halfway && (half & 1u) != 0))
                {
                    ++half;
                }
                return static_cast<std::uint16_t>(sign | half);
            }
            std::uint32_t half = (static_cast<std::uint32_t>(halfExponent) << 10) | (mantissa >> 13);
            const std::uint32_t remainder = mantissa & 0x1FFFu;
            if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u) != 0))
            {
                ++half;
            }
            return static_cast<std::uint16_t>(sign | half);
        }
    }

    Bool ParseColorLut(const char* text, std::size_t length, ColorLut& lut, String* error)
    {
        lut = ColorLut{};
        if (text == nullptr)
        {
            Fail(error, 0, "no text");
            return false;
        }
        const char* at = text;
        const char* end = text + length;
        // UTF-8 BOM 은 건너뛴다.
        if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF)
        {
            at += 3;
        }
        UInt32 line = 0;
        std::size_t expected = 0;
        while (at < end)
        {
            ++line;
            const char* lineEnd = at;
            while (lineEnd < end && *lineEnd != '\n')
            {
                ++lineEnd;
            }
            const char* next = lineEnd < end ? lineEnd + 1 : lineEnd;
            // 주석과 앞뒤 공백을 걷는다.
            const char* contentEnd = lineEnd;
            for (const char* scan = at; scan < lineEnd; ++scan)
            {
                if (*scan == '#')
                {
                    contentEnd = scan;
                    break;
                }
            }
            while (at < contentEnd && IsSpace(*at))
            {
                ++at;
            }
            while (contentEnd > at && IsSpace(contentEnd[-1]))
            {
                --contentEnd;
            }
            if (at == contentEnd)
            {
                at = next;
                continue;
            }
            const Bool numeric = (*at >= '0' && *at <= '9') || *at == '-' || *at == '+' || *at == '.';
            if (false == numeric)
            {
                if (StartsWith(at, contentEnd, "TITLE"))
                {
                    const char* title = at + 5;
                    while (title < contentEnd && (IsSpace(*title) || *title == '"'))
                    {
                        ++title;
                    }
                    const char* titleEnd = contentEnd;
                    while (titleEnd > title && titleEnd[-1] == '"')
                    {
                        --titleEnd;
                    }
                    lut.title.assign(title, static_cast<std::size_t>(titleEnd - title));
                }
                else if (StartsWith(at, contentEnd, "LUT_3D_SIZE"))
                {
                    Float size = 0.0f;
                    if (false == ReadFloats(at + 11, contentEnd, &size, 1) || size < 2.0f || size > 256.0f
                        || std::floor(size.Get()) != size.Get())
                    {
                        Fail(error, line, "LUT_3D_SIZE must be a whole number from 2 to 256");
                        return false;
                    }
                    lut.size = static_cast<JBro::UInt32>(static_cast<std::uint32_t>(size.Get()));
                    expected = static_cast<std::size_t>(lut.size.Get()) * lut.size.Get() * lut.size.Get() * 3u;
                    lut.rgb.Reserve(expected);
                }
                else if (StartsWith(at, contentEnd, "LUT_1D_SIZE"))
                {
                    Fail(error, line, "1D LUTs are not supported");
                    return false;
                }
                else if (StartsWith(at, contentEnd, "DOMAIN_MIN"))
                {
                    if (false == ReadFloats(at + 10, contentEnd, lut.domainMin, 3))
                    {
                        Fail(error, line, "DOMAIN_MIN needs three numbers");
                        return false;
                    }
                }
                else if (StartsWith(at, contentEnd, "DOMAIN_MAX"))
                {
                    if (false == ReadFloats(at + 10, contentEnd, lut.domainMax, 3))
                    {
                        Fail(error, line, "DOMAIN_MAX needs three numbers");
                        return false;
                    }
                }
                // 모르는 키(`LUT_3D_INPUT_RANGE` 따위)는 버린다.
                at = next;
                continue;
            }
            if (lut.size == 0)
            {
                Fail(error, line, "colour rows come before LUT_3D_SIZE");
                return false;
            }
            Float values[3] = {};
            if (false == ReadFloats(at, contentEnd, values, 3))
            {
                Fail(error, line, "a colour row needs three numbers");
                return false;
            }
            if (lut.rgb.Size() >= expected)
            {
                Fail(error, line, "more colour rows than LUT_3D_SIZE cubed");
                return false;
            }
            lut.rgb.Add(values[0]);
            lut.rgb.Add(values[1]);
            lut.rgb.Add(values[2]);
            at = next;
        }
        if (lut.size == 0)
        {
            Fail(error, line, "no LUT_3D_SIZE");
            return false;
        }
        if (lut.rgb.Size() != expected)
        {
            Fail(error, line, "fewer colour rows than LUT_3D_SIZE cubed");
            return false;
        }
        for (Int32 channel = 0; channel < 3; ++channel)
        {
            if (false == (lut.domainMax[channel.Get()] > lut.domainMin[channel.Get()]))
            {
                Fail(error, line, "DOMAIN_MAX must be above DOMAIN_MIN");
                return false;
            }
        }
        return true;
    }

    void PackColorLutRgba16(const ColorLut& lut, Array<std::byte>& texels)
    {
        const std::size_t cells = lut.rgb.Size() / 3u;
        texels.Resize(cells * 8u);
        const std::uint16_t one = ToHalf(1.0f);
        for (std::size_t cell = 0; cell < cells; ++cell)
        {
            const std::uint16_t texel[4] = {ToHalf(lut.rgb[cell * 3u]), ToHalf(lut.rgb[cell * 3u + 1u]), ToHalf(lut.rgb[cell * 3u + 2u]), one};
            std::memcpy(texels.Data() + cell * 8u, texel, sizeof(texel));
        }
    }
}
