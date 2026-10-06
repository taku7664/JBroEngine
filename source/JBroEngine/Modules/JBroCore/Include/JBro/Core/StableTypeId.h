#pragma once

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 문자열 이름으로부터 안정적인 64비트 타입 아이디를 만든다(FNV-1a 64).
    // 런타임 매직넘버 대신 T::StaticTypeName() 을 넣어서 쓴다.
    constexpr UInt64 MakeStableTypeId(const char* name)
    {
        UInt64 hash = 1469598103934665603ull;
        while (name != nullptr && *name != '\0')
        {
            hash ^= static_cast<JBro::UInt64>(static_cast<unsigned char>(*name));
            hash *= 1099511628211ull;
            ++name;
        }
        return hash;
    }

    using ComponentTypeId = UInt64;
    inline constexpr ComponentTypeId InvalidComponentTypeId = 0;
}
