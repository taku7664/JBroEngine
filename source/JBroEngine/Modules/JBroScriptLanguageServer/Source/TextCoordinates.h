#pragma once

#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <nlohmann/json.hpp>

// JSON is a private protocol boundary. Engine numeric wrappers keep their types internally.
namespace nlohmann
{
    template<typename Value>
    struct adl_serializer<JBro::IntegerType<Value>>
    {
        static void to_json(json& output, const JBro::IntegerType<Value>& value)
        {
            output = value.Get();
        }
        static void from_json(const json& input, JBro::IntegerType<Value>& value)
        {
            value.Set(input.template get<Value>());
        }
    };

    template<>
    struct adl_serializer<JBro::Bool>
    {
        static void to_json(json& output, JBro::Bool value)
        {
            output = value.Get();
        }
        static void from_json(const json& input, JBro::Bool& value)
        {
            value.Set(input.get<bool>());
        }
    };
}

namespace JBro::ScriptLanguageServer::Detail
{
    using Json = nlohmann::json;
    // Sorted spans (semantic tokens) translate in one pass, including Unicode and CRLF.
    class PositionCursor final
    {
    public:
        explicit PositionCursor(std::string_view text) : m_text(text), m_offset(text.starts_with("\xEF\xBB\xBF") ? 3 : 0) {}
        Json At(UInt32 offset);
    private:
        std::string_view m_text;
        std::size_t m_offset = 0;
        UInt32 m_line = 0;
        UInt32 m_character = 0;
    };
    Json Position(std::string_view text, UInt32 offset);
    Json Range(std::string_view text, const ScriptCompiler::SourceRange& range);
    Bool Offset(std::string_view text, const Json& position, UInt32& offset);
    String PathToUri(std::string_view path);
    Bool UriToPath(std::string_view uri, String& path);
    String NormalizeUri(std::string_view uri);
    String JsonString(const Json& object, const char* key, std::string_view fallback = {});
}
