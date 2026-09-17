#include <JBro/ScriptCompiler/DiagnosticMessages.h>

#include <JBro/Core/Yaml.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace JBro::ScriptCompiler
{
    namespace
    {
        bool LoadFile(const char* directory, const char* locale, Table<String, String>& out)
        {
            if (nullptr == directory || nullptr == locale || '\0' == *locale)
            {
                return false;
            }
            std::filesystem::path path(directory);
            path /= String(locale).Append(".yaml").Std();
            std::ifstream file(path, std::ios::binary);
            if (false == file.is_open())
            {
                return false;
            }
            const String text(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));

            YamlDocument document;
            YamlError error;
            if (false == document.Parse(text.c_str(), text.size(), error))
            {
                return false;
            }
            const std::uint32_t entries = document.Find(document.GetRoot(), "Entries");
            if (YamlDocument::InvalidNode == entries || document.GetKind(entries) != YamlKind::Map)
            {
                return false;
            }
            const std::size_t count = document.GetCount(entries);
            for (std::size_t index = 0; index < count; ++index)
            {
                const char* key = document.GetKey(entries, index);
                const std::uint32_t value = document.GetValue(entries, index);
                if (nullptr == key || YamlDocument::InvalidNode == value)
                {
                    continue;
                }
                const char* message = document.GetText(value);
                out.TryAdd(String(key), String(nullptr != message ? message : ""));
            }
            return true;
        }
    }

    bool DiagnosticMessages::Load(const char* directory, const char* locale, const char* fallbackLocale)
    {
        Table<String, String> entries;
        if (false == LoadFile(directory, locale, entries))
        {
            // 있던 표를 지우지 않는다. 파일 하나를 잘못 건드려 모든 메시지를 키로 잃을 이유가 없다.
            return false;
        }
        Table<String, String> fallbackEntries;
        if (nullptr != fallbackLocale && 0 != std::strcmp(fallbackLocale, locale))
        {
            LoadFile(directory, fallbackLocale, fallbackEntries);
        }
        m_entries = std::move(entries);
        m_fallbackEntries = std::move(fallbackEntries);
        m_locale = locale;
        return true;
    }

    const char* DiagnosticMessages::Find(const char* key) const
    {
        if (nullptr == key || '\0' == *key)
        {
            return nullptr;
        }
        const String lookup(key);
        if (const String* found = m_entries.Find(lookup))
        {
            return found->c_str();
        }
        if (const String* found = m_fallbackEntries.Find(lookup))
        {
            return found->c_str();
        }
        return nullptr;
    }

    String DiagnosticMessages::Format(const Diagnostic& diagnostic) const
    {
        const char* key = GetDiagnosticKey(diagnostic.Code);
        const char* found = Find(key);
        const std::string_view pattern = nullptr != found ? std::string_view(found) : std::string_view(key);

        String result;
        result.Reserve(pattern.size());
        for (std::size_t index = 0; index < pattern.size(); ++index)
        {
            const char c = pattern[index];
            // {n} 은 한 자리 숫자만 쓴다. 인자가 열 개를 넘는 메시지는 없다.
            const bool isPlaceholder = '{' == c && index + 2 < pattern.size()
                && pattern[index + 1] >= '0' && pattern[index + 1] <= '9' && '}' == pattern[index + 2];
            if (false == isPlaceholder)
            {
                result.push_back(c);
                continue;
            }
            const std::size_t argument = static_cast<std::size_t>(pattern[index + 1] - '0');
            if (argument < diagnostic.Arguments.Size())
            {
                result.Append(diagnostic.Arguments[argument].View());
            }
            else
            {
                // 인자가 모자라면 자리를 그대로 남겨 빠진 것이 보이게 한다.
                result.Append(pattern.substr(index, 3));
            }
            index += 2;
        }
        return result;
    }
}
