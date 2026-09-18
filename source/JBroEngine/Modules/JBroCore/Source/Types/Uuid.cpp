#include <JBro/Types/Uuid.h>

#include <random>

namespace JBro
{
    namespace
    {
        constexpr std::uint64_t VersionMask = 0xF000ull;
        constexpr std::uint64_t VariantMask = 0xC000000000000000ull;
        constexpr std::uint64_t Variant = 0x8000000000000000ull;

        constexpr Uuid WithVersion(std::uint64_t high, std::uint64_t low, std::uint64_t version) noexcept
        {
            Uuid result;
            result.high = (high & ~VersionMask) | ((version & 0xF) << 12);
            result.low = (low & ~VariantMask) | Variant;
            return result;
        }

        constexpr int HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        }

        // FNV-1a 64 를 두 시드로 돌려 128 비트를 채운다. 이름이 곧 아이디이므로 프로세스·플랫폼과 무관하다.
        constexpr std::uint64_t Fnv1a(const char* name, std::uint64_t seed) noexcept
        {
            std::uint64_t hash = seed;
            while (name != nullptr && *name != '\0')
            {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(*name));
                hash *= 1099511628211ull;
                ++name;
            }
            return hash;
        }
    }

    Uuid Uuid::Generate()
    {
        // `random_device` 는 운영체제의 난수원이다. 매번 새로 열어도 되지만 그러면 임포트 수백 개에서
        // 핸들을 수백 번 연다 - 한 번 열어 64 비트 엔진에 시드를 주고 그 엔진을 쓴다.
        static std::mt19937_64 engine = []
        {
            std::random_device source;
            std::seed_seq seeds{source(), source(), source(), source()};
            return std::mt19937_64(seeds);
        }();
        return WithVersion(engine(), engine(), 4);
    }

    Uuid Uuid::FromName(const char* name) noexcept
    {
        const std::uint64_t first = Fnv1a(name, 1469598103934665603ull);
        const std::uint64_t second = Fnv1a(name, 0x9E3779B97F4A7C15ull ^ first);
        return WithVersion(first, second, 8);
    }

    bool Uuid::ToText(char* buffer, std::size_t capacity) const noexcept
    {
        if (buffer == nullptr || capacity < TextCapacity)
        {
            return false;
        }
        constexpr char digits[] = "0123456789abcdef";
        const std::uint64_t words[2] = {high, low};
        std::size_t written = 0;
        for (std::uint64_t word : words)
        {
            for (int shift = 60; shift >= 0; shift -= 4)
            {
                buffer[written++] = digits[(word >> shift) & 0xF];
            }
        }
        buffer[written] = '\0';
        return true;
    }

    bool Uuid::Parse(const char* text, std::size_t length, Uuid& result) noexcept
    {
        if (text == nullptr)
        {
            return false;
        }
        // 하이픈은 8-4-4-4-12 자리에서만 받는다. 그 외 자리의 하이픈이나 다른 글자는 거절이다.
        constexpr std::size_t hyphenated = TextLength + 4;
        if (length != TextLength && length != hyphenated)
        {
            return false;
        }
        std::uint64_t words[2] = {0, 0};
        std::size_t digitIndex = 0;
        for (std::size_t index = 0; index < length; ++index)
        {
            const char c = text[index];
            if (c == '-')
            {
                const bool hyphenSlot = length == hyphenated
                    && (index == 8 || index == 13 || index == 18 || index == 23);
                if (false == hyphenSlot)
                {
                    return false;
                }
                continue;
            }
            const int value = HexValue(c);
            if (value < 0 || digitIndex >= TextLength)
            {
                return false;
            }
            words[digitIndex / 16] = (words[digitIndex / 16] << 4) | static_cast<std::uint64_t>(value);
            ++digitIndex;
        }
        if (digitIndex != TextLength)
        {
            return false;
        }
        result.high = words[0];
        result.low = words[1];
        return true;
    }
}
