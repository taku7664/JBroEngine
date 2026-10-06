#include <JBro/Package/PackageFormat.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    UInt64 Hash(const void* data, std::size_t size) noexcept
    {
        UInt64 hash = 0xCBF29CE484222325ull;
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
        UInt64 Word(UInt64 key, UInt64 block) noexcept
        {
            UInt64 z = key + (block + 1) * 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
    }

    void Obfuscate(UInt64 key, UInt64 position, void* data, std::size_t size) noexcept
    {
        auto* bytes = static_cast<unsigned char*>(data);
        UInt64 block = position / 8;
        UInt64 word = Word(key, block);
        for (std::size_t index = 0; index < size; ++index)
        {
            const UInt64 at = position + index;
            if (at / 8 != block)
            {
                block = at / 8;
                word = Word(key, block);
            }
            bytes[index] ^= static_cast<unsigned char>(word >> ((at % 8) * 8));
        }
    }

    Bool EntryLess(const Entry& left, const Entry& right) noexcept
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
