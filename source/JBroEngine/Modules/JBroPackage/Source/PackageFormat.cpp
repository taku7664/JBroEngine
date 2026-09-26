#include <JBro/Package/PackageFormat.h>

#include <cstring>

namespace JBro::Package
{
    std::uint64_t Hash(const void* data, std::size_t size) noexcept
    {
        std::uint64_t hash = 0xCBF29CE484222325ull;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 0x100000001B3ull;
        }
        return hash;
    }

    namespace
    {
        // splitmix64 다. 8 바이트 칸마다 칸 번호와 키로 한 낱말을 낸다 - 어느 칸에서든 앞 칸 없이 풀 수 있다.
        std::uint64_t Word(std::uint64_t key, std::uint64_t block) noexcept
        {
            std::uint64_t z = key + (block + 1) * 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
    }

    void Obfuscate(std::uint64_t key, std::uint64_t position, void* data, std::size_t size) noexcept
    {
        auto* bytes = static_cast<unsigned char*>(data);
        std::uint64_t block = position / 8;
        std::uint64_t word = Word(key, block);
        for (std::size_t index = 0; index < size; ++index)
        {
            const std::uint64_t at = position + index;
            if (at / 8 != block)
            {
                block = at / 8;
                word = Word(key, block);
            }
            bytes[index] ^= static_cast<unsigned char>(word >> ((at % 8) * 8));
        }
    }

    bool EntryLess(const Entry& left, const Entry& right) noexcept
    {
        if (left.id.high != right.id.high)
        {
            return left.id.high < right.id.high;
        }
        if (left.id.low != right.id.low)
        {
            return left.id.low < right.id.low;
        }
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    }
}
