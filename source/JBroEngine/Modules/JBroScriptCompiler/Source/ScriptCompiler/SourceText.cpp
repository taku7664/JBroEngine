#include <JBro/ScriptCompiler/SourceText.h>

#include <utility>

namespace JBro::ScriptCompiler
{
    UInt32 CharacterColumn(std::string_view text, const SourceLocation& at) noexcept
    {
        const std::size_t offset = at.Offset.Get();
        const std::size_t bytes = at.Column.Get() == 0 ? 0 : at.Column.Get() - 1;
        if (offset > text.size() || bytes > offset)
        {
            return at.Column;
        }
        UInt32 column = 1;
        for (std::size_t index = offset - bytes; index < offset; ++index)
        {
            // UTF-8 이어지는 바이트(10xxxxxx)는 글자를 새로 열지 않는다.
            if ((static_cast<unsigned char>(text[index]) & 0xC0u) != 0x80u)
            {
                ++column;
            }
        }
        return column;
    }

    SourceText::SourceText(String path, String text)
        : m_path(std::move(path))
        , m_text(std::move(text))
    {
    }
}
