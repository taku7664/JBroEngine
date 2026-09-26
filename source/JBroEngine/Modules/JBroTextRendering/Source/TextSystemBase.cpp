#include <JBro/TextRendering/TextSystemBase.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    void TextSystemBase::SetText(TextId& text, const char* utf8, std::uint32_t length)
    {
        TextStore::Get().Assign(text, utf8, utf8 != nullptr ? length : 0);
    }

    std::uint32_t TextSystemBase::GetTextLength(const TextId& text) const
    {
        return static_cast<std::uint32_t>(TextStore::Get().GetText(text).Size());
    }

    std::uint32_t TextSystemBase::CopyText(const TextId& text, char* buffer, std::uint32_t capacity) const
    {
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }
        const ArrayView<const char> source = TextStore::Get().GetText(text);
        const std::uint32_t count = static_cast<std::uint32_t>(std::min<std::size_t>(source.Size(), capacity - 1));
        if (count > 0)
        {
            std::memcpy(buffer, source.Data(), count);
        }
        buffer[count] = '\0';
        return count;
    }
}
