#include <JBro/TextRendering/TextSystemBase.h>

#include <algorithm>
#include <cstring>
#include <JBro/Types/UInt.h>

namespace JBro
{
    void TextSystemBase::SetText(TextId& text, const char* utf8, UInt32 length)
    {
        TextStore::Get().Assign(text, utf8, utf8 != nullptr ? length : UInt32(0));
    }

    UInt32 TextSystemBase::GetTextLength(const TextId& text) const
    {
        return static_cast<JBro::UInt32>(TextStore::Get().GetText(text).Size());
    }

    UInt32 TextSystemBase::CopyText(const TextId& text, char* buffer, UInt32 capacity) const
    {
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }
        const ArrayView<const char> source = TextStore::Get().GetText(text);
        const UInt32 count = static_cast<JBro::UInt32>(std::min<std::size_t>(source.Size(), capacity - 1));
        if (count > 0)
        {
            std::memcpy(buffer, source.Data(), count);
        }
        buffer[count] = '\0';
        return count;
    }
}
