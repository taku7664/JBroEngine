#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Types/Uuid.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        bool UuidToText(
            const void* value,
            char* buffer,
            std::size_t capacity,
            std::size_t& required) noexcept
        {
            required = Uuid::TextLength;
            char scratch[Uuid::TextCapacity];
            if (false == static_cast<const Uuid*>(value)->ToText(scratch, sizeof(scratch)))
            {
                required = 0;
                return false;
            }
            if (buffer == nullptr || capacity < Uuid::TextLength)
            {
                return false;
            }
            std::memcpy(buffer, scratch, Uuid::TextLength);
            return true;
        }

        bool UuidFromText(void* value, const char* text, std::size_t length) noexcept
        {
            Uuid parsed;
            if (false == Uuid::Parse(text, length, parsed))
            {
                return false;
            }
            *static_cast<Uuid*>(value) = parsed;
            return true;
        }

        bool UuidEquals(const void* left, const void* right) noexcept
        {
            return *static_cast<const Uuid*>(left) == *static_cast<const Uuid*>(right);
        }

        void UuidAssign(void* destination, const void* source) noexcept
        {
            *static_cast<Uuid*>(destination) = *static_cast<const Uuid*>(source);
        }
    }

    const ValueCodec& GetUuidCodec()
    {
        static const ValueCodec codec = []
        {
            ValueCodec result;
            result.ToText = &UuidToText;
            result.FromText = &UuidFromText;
            result.Equals = &UuidEquals;
            result.Assign = &UuidAssign;
            return result;
        }();
        return codec;
    }
}
