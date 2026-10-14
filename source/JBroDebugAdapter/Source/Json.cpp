#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace JBro::DebugAdapter
{
    namespace
    {
        const String EmptyString;

        class Parser
        {
        public:
            Parser(const char* text, std::size_t length)
                : m_cursor(text)
                , m_end(text + length)
            {
            }

            Bool ParseDocument(JsonValue& out)
            {
                if (false == ParseValue(out, 0))
                {
                    return false;
                }
                SkipSpace();
                return m_cursor == m_end;
            }

        private:
            // 깊이 상한이다. 남이 보낸 글자로 스택을 다 쓰지 않는다.
            static constexpr Int32 MaxDepth = 128;

            void SkipSpace()
            {
                while (m_cursor < m_end && (*m_cursor == ' ' || *m_cursor == '\t' || *m_cursor == '\r' || *m_cursor == '\n'))
                {
                    ++m_cursor;
                }
            }

            Bool Literal(const char* word)
            {
                const char* cursor = m_cursor;
                for (; *word != '\0'; ++word, ++cursor)
                {
                    if (cursor >= m_end || *cursor != *word)
                    {
                        return false;
                    }
                }
                m_cursor = cursor;
                return true;
            }

            Bool ParseValue(JsonValue& out, Int32 depth)
            {
                if (depth > MaxDepth)
                {
                    return false;
                }
                SkipSpace();
                if (m_cursor >= m_end)
                {
                    return false;
                }
                const char c = *m_cursor;
                if (c == '{')
                {
                    return ParseObject(out, depth);
                }
                if (c == '[')
                {
                    return ParseArray(out, depth);
                }
                if (c == '"')
                {
                    String text;
                    if (false == ParseString(text))
                    {
                        return false;
                    }
                    out = JsonValue(std::move(text));
                    return true;
                }
                if (Literal("true"))
                {
                    out = JsonValue(Bool(true));
                    return true;
                }
                if (Literal("false"))
                {
                    out = JsonValue(Bool(false));
                    return true;
                }
                if (Literal("null"))
                {
                    out = JsonValue();
                    return true;
                }
                return ParseNumber(out);
            }

            Bool ParseObject(JsonValue& out, Int32 depth)
            {
                ++m_cursor;
                out = JsonValue::MakeObject();
                SkipSpace();
                if (m_cursor < m_end && *m_cursor == '}')
                {
                    ++m_cursor;
                    return true;
                }
                while (true)
                {
                    SkipSpace();
                    String key;
                    if (m_cursor >= m_end || *m_cursor != '"' || false == ParseString(key))
                    {
                        return false;
                    }
                    SkipSpace();
                    if (m_cursor >= m_end || *m_cursor != ':')
                    {
                        return false;
                    }
                    ++m_cursor;
                    JsonValue value;
                    if (false == ParseValue(value, depth + 1))
                    {
                        return false;
                    }
                    out.Set(key.c_str(), std::move(value));
                    SkipSpace();
                    if (m_cursor < m_end && *m_cursor == ',')
                    {
                        ++m_cursor;
                        continue;
                    }
                    if (m_cursor < m_end && *m_cursor == '}')
                    {
                        ++m_cursor;
                        return true;
                    }
                    return false;
                }
            }

            Bool ParseArray(JsonValue& out, Int32 depth)
            {
                ++m_cursor;
                out = JsonValue::MakeArray();
                SkipSpace();
                if (m_cursor < m_end && *m_cursor == ']')
                {
                    ++m_cursor;
                    return true;
                }
                while (true)
                {
                    JsonValue value;
                    if (false == ParseValue(value, depth + 1))
                    {
                        return false;
                    }
                    out.Add(std::move(value));
                    SkipSpace();
                    if (m_cursor < m_end && *m_cursor == ',')
                    {
                        ++m_cursor;
                        continue;
                    }
                    if (m_cursor < m_end && *m_cursor == ']')
                    {
                        ++m_cursor;
                        return true;
                    }
                    return false;
                }
            }

            static void AppendUtf8(String& out, std::uint32_t codePoint)
            {
                if (codePoint < 0x80)
                {
                    out += static_cast<char>(codePoint);
                }
                else if (codePoint < 0x800)
                {
                    out += static_cast<char>(0xC0 | (codePoint >> 6));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else if (codePoint < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (codePoint >> 12));
                    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (codePoint >> 18));
                    out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
            }

            Bool ParseHex4(std::uint32_t& value)
            {
                if (m_end - m_cursor < 4)
                {
                    return false;
                }
                value = 0;
                for (Int32 i = 0; i < 4; ++i)
                {
                    const char c = *m_cursor++;
                    value <<= 4;
                    if (c >= '0' && c <= '9')
                    {
                        value |= static_cast<std::uint32_t>(c - '0');
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        value |= static_cast<std::uint32_t>(c - 'a' + 10);
                    }
                    else if (c >= 'A' && c <= 'F')
                    {
                        value |= static_cast<std::uint32_t>(c - 'A' + 10);
                    }
                    else
                    {
                        return false;
                    }
                }
                return true;
            }

            Bool ParseString(String& out)
            {
                ++m_cursor;
                while (m_cursor < m_end)
                {
                    const char c = *m_cursor++;
                    if (c == '"')
                    {
                        return true;
                    }
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (m_cursor >= m_end)
                    {
                        return false;
                    }
                    const char escape = *m_cursor++;
                    switch (escape)
                    {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        std::uint32_t codePoint = 0;
                        if (false == ParseHex4(codePoint))
                        {
                            return false;
                        }
                        // 서로게이트 쌍이면 뒤의 반쪽을 마저 읽는다.
                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && m_end - m_cursor >= 6 && m_cursor[0] == '\\'
                            && m_cursor[1] == 'u')
                        {
                            m_cursor += 2;
                            std::uint32_t low = 0;
                            if (false == ParseHex4(low))
                            {
                                return false;
                            }
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        }
                        AppendUtf8(out, codePoint);
                        break;
                    }
                    default:
                        return false;
                    }
                }
                return false;
            }

            Bool ParseNumber(JsonValue& out)
            {
                const char* start = m_cursor;
                while (m_cursor < m_end && (*m_cursor == '-' || *m_cursor == '+' || *m_cursor == '.' || *m_cursor == 'e'
                                               || *m_cursor == 'E' || (*m_cursor >= '0' && *m_cursor <= '9')))
                {
                    ++m_cursor;
                }
                if (m_cursor == start)
                {
                    return false;
                }
                const String text(start, static_cast<std::size_t>(m_cursor - start));
                char* parsedEnd = nullptr;
                const double value = std::strtod(text.c_str(), &parsedEnd);
                if (parsedEnd != text.c_str() + text.size())
                {
                    return false;
                }
                out = JsonValue(value);
                return true;
            }

            const char* m_cursor;
            const char* m_end;
        };

        void AppendQuoted(String& out, const String& text)
        {
            out += '"';
            for (const char c : text)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char escaped[8] = {};
                        std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                        out += escaped;
                    }
                    else
                    {
                        out += c;
                    }
                    break;
                }
            }
            out += '"';
        }
    }

    JsonValue::JsonValue(Bool value)
        : m_kind(Kind::Bool)
        , m_bool(value)
    {
    }

    JsonValue::JsonValue(Int64 value)
        : m_kind(Kind::Number)
        , m_number(static_cast<double>(value.Value))
    {
    }

    JsonValue::JsonValue(Int32 value)
        : m_kind(Kind::Number)
        , m_number(static_cast<double>(value.Value))
    {
    }

    JsonValue::JsonValue(double value)
        : m_kind(Kind::Number)
        , m_number(value)
    {
    }

    JsonValue::JsonValue(const char* value)
        : m_kind(Kind::String)
        , m_string(value != nullptr ? value : "")
    {
    }

    JsonValue::JsonValue(String value)
        : m_kind(Kind::String)
        , m_string(std::move(value))
    {
    }

    JsonValue JsonValue::MakeArray()
    {
        JsonValue value;
        value.m_kind = Kind::Array;
        return value;
    }

    JsonValue JsonValue::MakeObject()
    {
        JsonValue value;
        value.m_kind = Kind::Object;
        return value;
    }

    JsonValue& JsonValue::Set(const char* key, JsonValue value)
    {
        if (m_kind != Kind::Object)
        {
            *this = MakeObject();
        }
        for (std::size_t index = 0; index < m_keys.Size(); ++index)
        {
            if (m_keys[index] == key)
            {
                *m_items[index] = std::move(value);
                return *this;
            }
        }
        m_keys.Add(String(key));
        m_items.Add(MakeOwnerPtr<JsonValue>(std::move(value)));
        return *this;
    }

    const JsonValue* JsonValue::Find(const char* key) const
    {
        if (m_kind != Kind::Object)
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_keys.Size(); ++index)
        {
            if (m_keys[index] == key)
            {
                return m_items[index].Get();
            }
        }
        return nullptr;
    }

    JsonValue& JsonValue::Add(JsonValue value)
    {
        if (m_kind != Kind::Array)
        {
            *this = MakeArray();
        }
        m_items.Add(MakeOwnerPtr<JsonValue>(std::move(value)));
        return *this;
    }

    Bool JsonValue::AsBool(Bool fallback) const
    {
        return m_kind == Kind::Bool ? m_bool : fallback;
    }

    Int64 JsonValue::AsInt64(Int64 fallback) const
    {
        return m_kind == Kind::Number ? Int64(static_cast<std::int64_t>(m_number)) : fallback;
    }

    double JsonValue::AsNumber(double fallback) const
    {
        return m_kind == Kind::Number ? m_number : fallback;
    }

    const String& JsonValue::AsString() const
    {
        return m_kind == Kind::String ? m_string : EmptyString;
    }

    String JsonValue::GetString(const char* key, const char* fallback) const
    {
        const JsonValue* value = Find(key);
        return value != nullptr && value->m_kind == Kind::String ? value->m_string : String(fallback);
    }

    Int64 JsonValue::GetInt64(const char* key, Int64 fallback) const
    {
        const JsonValue* value = Find(key);
        return value != nullptr ? value->AsInt64(fallback) : fallback;
    }

    Bool JsonValue::GetBool(const char* key, Bool fallback) const
    {
        const JsonValue* value = Find(key);
        return value != nullptr ? value->AsBool(fallback) : fallback;
    }

    String JsonValue::Serialize() const
    {
        String out;
        SerializeTo(out);
        return out;
    }

    void JsonValue::SerializeTo(String& out) const
    {
        switch (m_kind)
        {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += m_bool ? "true" : "false";
            break;
        case Kind::Number:
        {
            char text[32] = {};
            if (std::isfinite(m_number) && m_number == std::floor(m_number) && std::fabs(m_number) < 9.0e15)
            {
                std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(m_number));
            }
            else if (std::isfinite(m_number))
            {
                std::snprintf(text, sizeof(text), "%.17g", m_number);
            }
            else
            {
                std::snprintf(text, sizeof(text), "0");
            }
            out += text;
            break;
        }
        case Kind::String:
            AppendQuoted(out, m_string);
            break;
        case Kind::Array:
            out += '[';
            for (std::size_t index = 0; index < m_items.Size(); ++index)
            {
                if (index > 0)
                {
                    out += ',';
                }
                m_items[index]->SerializeTo(out);
            }
            out += ']';
            break;
        case Kind::Object:
            out += '{';
            for (std::size_t index = 0; index < m_keys.Size(); ++index)
            {
                if (index > 0)
                {
                    out += ',';
                }
                AppendQuoted(out, m_keys[index]);
                out += ':';
                m_items[index]->SerializeTo(out);
            }
            out += '}';
            break;
        }
    }

    Bool JsonValue::Parse(const char* text, std::size_t length, JsonValue& out)
    {
        if (text == nullptr)
        {
            return false;
        }
        Parser parser(text, length);
        return parser.ParseDocument(out);
    }
}
