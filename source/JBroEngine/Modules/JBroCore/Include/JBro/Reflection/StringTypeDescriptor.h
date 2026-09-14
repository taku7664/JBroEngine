#pragma once

#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/String.h>

#include <cstring>

namespace JBro
{
    // `String` 은 **잎사귀**다. 필드를 내놓지 않고 글자로 오간다.
    //
    // 코덱의 `Assign` 이 여기서 특히 중요하다. `memcpy` 로 옮기면 내용이 밖에 있는
    // 타입은 얕은 복사가 되어 먼저 죽는 쪽이 남은 쪽을 망가뜨린다 - 코덱 계약이
    // 그것을 못 하게 막으려고 있다.
    inline const ValueCodec& GetStringCodec()
    {
        static const ValueCodec codec = [] {
            ValueCodec made;
            made.ToText = [](const void* value, char* buffer, std::size_t capacity,
                std::size_t& required) noexcept -> bool {
                const String& text = *static_cast<const String*>(value);
                required = text.size() + 1;
                if (buffer == nullptr || capacity < required)
                {
                    // 모자란 버퍼에 반쪽을 써 주지 않는다. 잘린 글자를 저장하면
                    // 되읽을 때 조용히 다른 값이 된다.
                    return false;
                }
                std::memcpy(buffer, text.c_str(), text.size());
                buffer[text.size()] = '\0';
                return true;
            };
            made.FromText = [](void* value, const char* text,
                std::size_t length) noexcept -> bool {
                if (text == nullptr)
                {
                    return false;
                }
                try
                {
                    *static_cast<String*>(value) = String(text, length);
                    return true;
                }
                catch (...)
                {
                    // 자리를 못 잡았다. 값은 건드리지 않은 채로 실패다.
                    return false;
                }
            };
            made.Equals = [](const void* left, const void* right) noexcept -> bool {
                return *static_cast<const String*>(left)
                    == *static_cast<const String*>(right);
            };
            made.Assign = [](void* destination, const void* source) noexcept {
                try
                {
                    *static_cast<String*>(destination) =
                        *static_cast<const String*>(source);
                }
                catch (...)
                {
                    // 복사가 실패해도 예외는 경계를 넘지 않는다. 받는 쪽은
                    // 옛 값을 그대로 들고 있게 된다.
                }
            };
            return made;
        }();
        return codec;
    }

    template <>
    struct TypeDescriptorOf<String>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("string");
                made.size = static_cast<std::uint32_t>(sizeof(String));
                made.alignment = static_cast<std::uint32_t>(alignof(String));
                made.triviallyCopyable = false;
                made.codec = &GetStringCodec();
                return made;
            }();
            return descriptor;
        }
    };
}
