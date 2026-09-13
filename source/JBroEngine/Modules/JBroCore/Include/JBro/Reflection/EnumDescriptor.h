#pragma once

#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/NameTable.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace JBro
{
    // enum 값 하나와 그 이름. 값을 적는 이유는 **연속이 아닐 수 있기 때문이다** —
    // 인덱스로 값을 만들어 내면 `enum { A = 1, B = 4 }` 에서 조용히 틀린다.
    template <typename E>
    struct EnumEntry
    {
        E           value;
        const char* name;
    };

    namespace Detail
    {
        // enum 하나의 값·이름 배열. `EnumNames` 의 함수 포인터는 인자를 못 받으므로
        // 이 표를 정적으로 둘 자리가 필요하다.
        template <typename E, std::size_t Count>
        struct EnumTable
        {
            static inline E           Values[Count] {};
            static inline const char* Names [Count] {};
        };
    }

    // enum 하나의 설명서를 만든다. 처음 물어볼 때 한 번 만들고 그 뒤로 같은 주소다 —
    // `PropertyInfo::type` 이 그 주소를 들고 다닌다.
    //
    // **저장 파일에는 숫자가 아니라 이름이 적힌다.** 숫자로 적으면 나중에 값 하나를
    // 가운데 끼워 넣는 순간 예전 파일이 전부 한 칸씩 밀린다.
    template <typename E, std::size_t Count>
    const TypeDescriptor& DefineEnumTypeDescriptor(
        const char* typeName,
        const EnumEntry<E> (&entries)[Count])
    {
        static_assert(std::is_enum_v<E>, "an enum descriptor needs an enum type");
        static_assert(Count > 0, "an enum with no named values cannot be saved or shown");

        using Table = Detail::EnumTable<E, Count>;

        static const TypeDescriptor descriptor = [&]() -> TypeDescriptor
        {
            for (std::size_t i = 0; i < Count; ++i)
            {
                Table::Values[i] = entries[i].value;
                Table::Names [i] = entries[i].name;
            }

            static const EnumNames names = []
            {
                EnumNames result;
                result.names = Table::Names;
                result.count = static_cast<std::uint32_t>(Count);
                result.ToIndex = [](const void* value) noexcept -> std::int32_t
                {
                    const E held = *static_cast<const E*>(value);
                    for (std::size_t i = 0; i < Count; ++i)
                    {
                        if (Table::Values[i] == held)
                        {
                            return static_cast<std::int32_t>(i);
                        }
                    }
                    // 이름 없는 값이다. 지어내지 않는다.
                    return -1;
                };
                result.FromIndex = [](void* value, std::int32_t index) noexcept
                {
                    if (index < 0 || static_cast<std::size_t>(index) >= Count)
                    {
                        return;
                    }
                    *static_cast<E*>(value) = Table::Values[index];
                };
                return result;
            }();

            static const ValueCodec codec = []
            {
                ValueCodec result;
                result.ToText = [](
                    const void* value,
                    char* buffer,
                    std::size_t capacity,
                    std::size_t& required) noexcept -> bool
                {
                    const E held = *static_cast<const E*>(value);
                    for (std::size_t i = 0; i < Count; ++i)
                    {
                        if (Table::Values[i] != held)
                        {
                            continue;
                        }
                        const std::size_t length = std::strlen(Table::Names[i]);
                        required = length;
                        if (buffer == nullptr || capacity < length)
                        {
                            return false;
                        }
                        std::memcpy(buffer, Table::Names[i], length);
                        return true;
                    }
                    // 이름 없는 값은 숫자로 흘려 쓰지 않는다. 그러면 되읽을 때
                    // 그것이 이름인지 숫자인지 파일만 보고는 알 수 없다.
                    required = 0;
                    return false;
                };
                result.FromText = [](void* value, const char* text, std::size_t length) noexcept -> bool
                {
                    if (text == nullptr || length == 0)
                    {
                        return false;
                    }
                    for (std::size_t i = 0; i < Count; ++i)
                    {
                        if (std::strlen(Table::Names[i]) != length)
                        {
                            continue;
                        }
                        if (std::memcmp(text, Table::Names[i], length) == 0)
                        {
                            *static_cast<E*>(value) = Table::Values[i];
                            return true;
                        }
                    }
                    // 모르는 이름이다. 값을 건드리지 않는다 —
                    // 첫 번째 값으로 떨어뜨리면 지워진 enum 값이 조용히 다른 것이 된다.
                    return false;
                };
                result.Equals = [](const void* left, const void* right) noexcept -> bool
                {
                    return *static_cast<const E*>(left) == *static_cast<const E*>(right);
                };
                result.Assign = [](void* destination, const void* source) noexcept
                {
                    *static_cast<E*>(destination) = *static_cast<const E*>(source);
                };
                return result;
            }();

            TypeDescriptor built;
            built.typeName = NameTable::Get().Intern(typeName);
            built.size = static_cast<std::uint32_t>(sizeof(E));
            built.alignment = static_cast<std::uint32_t>(alignof(E));
            built.triviallyCopyable = true;
            built.enumNames = &names;
            built.codec = &codec;
            return built;
        }();

        return descriptor;
    }

    // enum 하나를 `TypeDescriptorOf` 에 잇는다. 값 목록은 그 enum 이 사는 모듈이 준다 —
    // Core 가 Framework 의 enum 을 알 필요가 없다.
    //
    //     namespace JBro {
    //         JBRO_DEFINE_ENUM_TYPE(Component::SpriteFlip, "Component::SpriteFlip",
    //             { Component::SpriteFlip::None,       "None" },
    //             { Component::SpriteFlip::Horizontal, "Horizontal" });
    //     }
    #define JBRO_DEFINE_ENUM_TYPE(Type, TypeNameLiteral, ...)                       \
        template <>                                                                 \
        struct TypeDescriptorOf<Type>                                               \
        {                                                                           \
            static const TypeDescriptor& Get()                                      \
            {                                                                       \
                static const ::JBro::EnumEntry<Type> entries[] = { __VA_ARGS__ };    \
                return ::JBro::DefineEnumTypeDescriptor<Type>(                       \
                    TypeNameLiteral, entries);                                      \
            }                                                                       \
        }
}
