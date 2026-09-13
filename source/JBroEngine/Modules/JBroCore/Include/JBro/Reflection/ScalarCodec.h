#pragma once

#include <JBro/Reflection/TypeDescriptor.h>

#include <charconv>
#include <cstring>
#include <type_traits>

namespace JBro
{
    // 산술 타입 하나의 코덱을 만든다. 생성되는 함수들은 이 번역 단위에 놓이므로,
    // 스크립트 DLL 이 이것을 쓰면 그 코드는 DLL 안에 있고 호스트는 주소만 부른다.
    //
    // 로캘을 타지 않는다. `std::to_chars` / `from_chars` 는 언제나 C 로캘처럼 동작하므로
    // 저장 파일이 사용자 지역 설정에 따라 `3.14` 와 `3,14` 로 갈리지 않는다.
    template<typename T>
    const ValueCodec& GetScalarCodec()
    {
        static_assert(std::is_arithmetic_v<T>, "a scalar codec needs an arithmetic type");

        static const ValueCodec codec = []
        {
            ValueCodec result;

            result.ToText = [](
                const void* value,
                char* buffer,
                std::size_t capacity,
                std::size_t& required) noexcept -> bool
            {
                // 넉넉한 임시 자리에 먼저 쓴다. 그래야 버퍼가 모자랄 때
                // 필요한 크기를 정확히 알려 줄 수 있다.
                char scratch[64];
                const std::to_chars_result written =
                    std::to_chars(scratch, scratch + sizeof(scratch), *static_cast<const T*>(value));
                if (written.ec != std::errc{})
                {
                    required = 0;
                    return false;
                }
                const std::size_t length = static_cast<std::size_t>(written.ptr - scratch);
                required = length;
                if (buffer == nullptr || capacity < length)
                {
                    return false;
                }
                std::memcpy(buffer, scratch, length);
                return true;
            };

            result.FromText = [](void* value, const char* text, std::size_t length) noexcept -> bool
            {
                if (text == nullptr || length == 0)
                {
                    return false;
                }
                T parsed{};
                const std::from_chars_result read = std::from_chars(text, text + length, parsed);
                // 일부만 읽고 남으면 실패로 본다 — "12abc" 를 12 로 받아들이지 않는다.
                if (read.ec != std::errc{} || read.ptr != text + length)
                {
                    return false;
                }
                *static_cast<T*>(value) = parsed;
                return true;
            };

            result.Equals = [](const void* left, const void* right) noexcept -> bool
            {
                return *static_cast<const T*>(left) == *static_cast<const T*>(right);
            };

            result.Assign = [](void* destination, const void* source) noexcept
            {
                *static_cast<T*>(destination) = *static_cast<const T*>(source);
            };

            return result;
        }();
        return codec;
    }

    // bool 은 따로다. `from_chars` 가 bool 을 받지 않고, 저장 파일에는 0/1 이 아니라
    // true/false 로 적혀야 사람이 읽을 수 있다.
    const ValueCodec& GetBoolCodec();

    // 산술 타입의 TypeDescriptor 를 만든다. 이름은 부르는 쪽이 준다 —
    // `float` 인지 `JBro.Degree` 인지는 타입만으로 알 수 없기 때문이다.
    template<typename T>
    TypeDescriptor MakeScalarTypeDescriptor(const char* typeName)
    {
        TypeDescriptor descriptor;
        descriptor.typeName = NameTable::Get().Intern(typeName);
        descriptor.size = static_cast<std::uint32_t>(sizeof(T));
        descriptor.alignment = static_cast<std::uint32_t>(alignof(T));
        descriptor.triviallyCopyable = std::is_trivially_copyable_v<T>;
        if constexpr (std::is_same_v<T, bool>)
        {
            descriptor.codec = &GetBoolCodec();
        }
        else
        {
            descriptor.codec = &GetScalarCodec<T>();
        }
        return descriptor;
    }
}
