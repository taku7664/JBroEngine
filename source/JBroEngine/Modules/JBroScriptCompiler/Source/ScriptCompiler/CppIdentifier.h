#pragma once

#include <JBro/Types/String.h>

#include <string_view>

namespace JBro::ScriptCompiler
{
    // This mapping is injective: script names beginning with the generated prefix
    // are escaped too, so a keyword and a user spelling cannot share a C++ name.
    inline String CppIdentifier(std::string_view name)
    {
        constexpr std::string_view keywords[] = {
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
            "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
            "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
            "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
            "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
            "explicit", "export", "extern", "false", "final", "float", "for", "friend",
            "goto", "if", "import", "inline", "int", "long", "module", "mutable",
            "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
            "or", "or_eq", "override", "private", "protected", "public", "register",
            "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
            "static", "static_assert", "static_cast", "struct", "switch", "template",
            "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
            "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
            "wchar_t", "while", "xor", "xor_eq"
        };
        constexpr std::string_view hiddenNames[] = {
            "GetTypeId", "StaticTypeName", "JBroFieldAt", "JBroFieldBase", "JBroSelf",
            "JBro", "std"
        };
        bool keyword = false;
        for (std::string_view candidate : keywords)
        {
            if (name == candidate) { keyword = true; break; }
        }
        for (std::string_view candidate : hiddenNames)
        {
            if (name == candidate) { keyword = true; break; }
        }
        const bool generatedPrefix = name.rfind("jbro_gen_", 0) == 0;
        bool unsafe = name.empty() || name.front() == '_'
            || (name.front() >= '0' && name.front() <= '9')
            || name.find("__") != std::string_view::npos;
        for (unsigned char character : name)
        {
            if (!((character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9') || character == '_'))
            {
                unsafe = true;
            }
        }
        if (!keyword && !generatedPrefix && !unsafe) return String(name);
        if (!unsafe) return "jbro_gen_" + String(name);

        constexpr char hex[] = "0123456789abcdef";
        String escaped = "jbro_gen_hex_";
        for (unsigned char character : name)
        {
            escaped += hex[character >> 4];
            escaped += hex[character & 0x0f];
        }
        return escaped;
    }
}
