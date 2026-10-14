#include "TextCoordinates.h"

#include <algorithm>
#include <cctype>

namespace JBro::ScriptLanguageServer::Detail
{
    namespace
    {
        UInt32 Decode(std::string_view text, std::size_t& index)
        {
            const unsigned char first = static_cast<unsigned char>(text[index++]);
            if (first < 0x80)
            {
                return first;
            }
            UInt32 code = first & (first < 0xE0 ? 0x1F : first < 0xF0 ? 0x0F : 0x07);
            const std::size_t count = first < 0xE0 ? 1 : first < 0xF0 ? 2 : 3;
            if (index + count > text.size())
            {
                return 0xFFFD;
            }
            for (std::size_t part = 0; part < count; ++part)
            {
                const unsigned char next = static_cast<unsigned char>(text[index]);
                if ((next & 0xC0) != 0x80)
                {
                    return 0xFFFD;
                }
                ++index;
                code = (code << 6) | (next & 0x3F);
            }
            return code;
        }

        Int32 Hex(char character)
        {
            if (character >= '0' && character <= '9')
            {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f')
            {
                return character - 'a' + 10;
            }
            if (character >= 'A' && character <= 'F')
            {
                return character - 'A' + 10;
            }
            return -1;
        }
    }

    Json PositionCursor::At(UInt32 offset)
    {
        const std::size_t end = std::min(static_cast<std::size_t>(offset), m_text.size());
        if (end < m_offset)
        {
            m_offset = m_text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
            m_line = 0;
            m_character = 0;
        }
        while (m_offset < end)
        {
            if (m_text[m_offset] == '\n')
            {
                ++m_offset;
                ++m_line;
                m_character = 0;
            }
            else if (m_text[m_offset] == '\r' && m_offset + 1 < m_text.size() && m_text[m_offset + 1] == '\n')
            {
                ++m_offset;
            }
            else
            {
                const UInt32 code = Decode(m_text, m_offset);
                m_character += code > 0xFFFF ? 2 : 1;
            }
        }
        return Json{{"line", m_line}, {"character", m_character}};
    }

    Json Position(std::string_view text, UInt32 offset)
    {
        return PositionCursor(text).At(offset);
    }

    Json Range(std::string_view text, const ScriptCompiler::SourceRange& range)
    {
        return Json{{"start", Position(text, range.Begin.Offset)}, {"end", Position(text, range.End.Offset)}};
    }

    Bool Offset(std::string_view text, const Json& position, UInt32& offset)
    {
        if (false == position.is_object() || false == position.contains("line") || false == position.contains("character") ||
            false == position["line"].is_number_integer() || false == position["character"].is_number_integer())
        {
            return false;
        }
        const Int64 desiredLine = position["line"].get<Int64>();
        const Int64 desiredCharacter = position["character"].get<Int64>();
        if (desiredLine < 0 || desiredCharacter < 0)
        {
            return false;
        }
        // Like the lexer, treat a leading UTF-8 BOM as encoding metadata, not a visible column.
        std::size_t index = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
        Int64 line = 0;
        while (line < desiredLine && index < text.size())
        {
            if (text[index++] == '\n')
            {
                ++line;
            }
        }
        if (line != desiredLine)
        {
            return false;
        }
        Int64 character = 0;
        while (character < desiredCharacter && index < text.size() && text[index] != '\n' && text[index] != '\r')
        {
            const std::size_t before = index;
            const UInt32 code = Decode(text, index);
            const Int64 units = code > 0xFFFF ? 2 : 1;
            if (character + units > desiredCharacter)
            {
                index = before;
                return false;
            }
            character += units;
        }
        if (index > UInt32::MaxValue().Get())
        {
            return false;
        }
        offset = static_cast<UInt32>(index);
        return true;
    }

    String PathToUri(std::string_view path)
    {
        const char digits[] = "0123456789ABCDEF";
        String result(path.starts_with("//") || path.starts_with("\\\\") ? "file:" : "file:///");
        if (path.starts_with('/'))
        {
            result = "file://";
        }
        for (unsigned char character : path)
        {
            if (character == '\\')
            {
                result += '/';
            }
            else if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '/' || character == ':' || character == '-' ||
                character == '_' || character == '.' || character == '~')
            {
                result += static_cast<char>(character);
            }
            else
            {
                result += '%';
                result += digits[character >> 4];
                result += digits[character & 15];
            }
        }
        return result;
    }

    Bool UriToPath(std::string_view uri, String& path)
    {
        if (false == uri.starts_with("file://"))
        {
            return false;
        }
        std::string_view encoded = uri.substr(7);
        const Bool local = encoded.starts_with('/');
        path.Clear();
        if (encoded.starts_with("localhost/"))
        {
            encoded.remove_prefix(9);
        }
        else if (false == local)
        {
            path = "//";
        }
        else
        {
            path.Clear();
        }
        for (std::size_t index = 0; index < encoded.size(); ++index)
        {
            if (encoded[index] == '%')
            {
                if (index + 2 >= encoded.size() || Hex(encoded[index + 1]) < 0 || Hex(encoded[index + 2]) < 0)
                {
                    return false;
                }
                const char decoded = static_cast<char>((Hex(encoded[index + 1]) << 4) | Hex(encoded[index + 2]));
                if (decoded == '\0')
                {
                    return false;
                }
                path += decoded;
                index += 2;
            }
            else
            {
                if (encoded[index] == '?' || encoded[index] == '#')
                {
                    return false;
                }
                path += encoded[index];
            }
        }
        if (local && path.size() >= 3 && path[0] == '/'
            && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
        {
            path.erase(0, 1);
        }
        return true;
    }

    String NormalizeUri(std::string_view uri)
    {
        String path;
        if (UriToPath(uri, path))
        {
            if (path.size() > 1 && path[1] == ':')
            {
                path[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
            }
            return PathToUri(path);
        }
        return String(uri);
    }

    String JsonString(const Json& object, const char* key, std::string_view fallback)
    {
        const auto item = object.find(key);
        return item != object.end() && item->is_string() ? String(item->get<std::string>()) : String(fallback);
    }
}
