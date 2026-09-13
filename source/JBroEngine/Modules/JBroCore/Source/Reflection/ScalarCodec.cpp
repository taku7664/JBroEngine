#include <JBro/Reflection/ScalarCodec.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        constexpr char TrueText[] = "true";
        constexpr char FalseText[] = "false";

        bool BoolToText(
            const void* value,
            char* buffer,
            std::size_t capacity,
            std::size_t& required) noexcept
        {
            const bool state = *static_cast<const bool*>(value);
            const char* text = state ? TrueText : FalseText;
            const std::size_t length = state ? sizeof(TrueText) - 1 : sizeof(FalseText) - 1;
            required = length;
            if (buffer == nullptr || capacity < length)
            {
                return false;
            }
            std::memcpy(buffer, text, length);
            return true;
        }

        bool BoolFromText(void* value, const char* text, std::size_t length) noexcept
        {
            if (text == nullptr)
            {
                return false;
            }
            if (length == sizeof(TrueText) - 1
                && std::memcmp(text, TrueText, length) == 0)
            {
                *static_cast<bool*>(value) = true;
                return true;
            }
            if (length == sizeof(FalseText) - 1
                && std::memcmp(text, FalseText, length) == 0)
            {
                *static_cast<bool*>(value) = false;
                return true;
            }
            // 0/1 은 받지 않는다. 저장 파일의 표기를 하나로 둔다.
            return false;
        }

        bool BoolEquals(const void* left, const void* right) noexcept
        {
            return *static_cast<const bool*>(left) == *static_cast<const bool*>(right);
        }

        void BoolAssign(void* destination, const void* source) noexcept
        {
            *static_cast<bool*>(destination) = *static_cast<const bool*>(source);
        }
    }

    const ValueCodec& GetBoolCodec()
    {
        static const ValueCodec codec = []
        {
            ValueCodec result;
            result.ToText = &BoolToText;
            result.FromText = &BoolFromText;
            result.Equals = &BoolEquals;
            result.Assign = &BoolAssign;
            return result;
        }();
        return codec;
    }
}
