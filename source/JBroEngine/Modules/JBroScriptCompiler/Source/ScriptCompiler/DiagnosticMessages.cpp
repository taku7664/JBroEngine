#include <JBro/ScriptCompiler/DiagnosticMessages.h>

#include <JBro/Core/Yaml.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::ScriptCompiler
{
    namespace
    {
        Bool LoadFile(const char* directory, const char* locale, Table<String, String>& out)
        {
            if (nullptr == directory || nullptr == locale || '\0' == *locale)
            {
                return false;
            }
            // **경로는 UTF-8 이다.** `path(const char*)` 는 시스템 코드 페이지(한국어 Windows 는 949)로 읽어서,
            // 한글이 든 UTF-8 경로를 넘기면 변환 예외가 난다. char8_t 로 넘겨 UTF-8 로 읽게 한다.
            const std::string file8 = String(directory).Append("/").Append(locale).Append(".yaml").Std();
            const std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t*>(file8.data()), file8.size()));
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
            const UInt32 entries = document.Find(document.GetRoot(), "Entries");
            if (YamlDocument::InvalidNode == entries || document.GetKind(entries) != YamlKind::Map)
            {
                return false;
            }
            const std::size_t count = document.GetCount(entries);
            for (std::size_t index = 0; index < count; ++index)
            {
                const char* key = document.GetKey(entries, index);
                const UInt32 value = document.GetValue(entries, index);
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

    Bool DiagnosticMessages::Load(const char* directory, const char* locale, const char* fallbackLocale)
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
        return FormatKey(GetDiagnosticKey(diagnostic.Code),
            ArrayView<const String>(diagnostic.Arguments.Data(), diagnostic.Arguments.Size()));
    }

    String DiagnosticMessages::FormatKey(const char* key, ArrayView<const String> arguments) const
    {
        const char* found = Find(key);
        const std::string_view pattern = nullptr != found ? std::string_view(found) : std::string_view(key);

        String result;
        result.Reserve(pattern.size());
        for (std::size_t index = 0; index < pattern.size(); ++index)
        {
            const char c = pattern[index];
            // {n} 은 한 자리 숫자만 쓴다. 인자가 열 개를 넘는 메시지는 없다.
            const Bool isPlaceholder = '{' == c && index + 2 < pattern.size()
                && pattern[index + 1] >= '0' && pattern[index + 1] <= '9' && '}' == pattern[index + 2];
            if (false == isPlaceholder)
            {
                result.push_back(c);
                continue;
            }
            const std::size_t argument = static_cast<std::size_t>(pattern[index + 1] - '0');
            if (argument < arguments.Size())
            {
                result.Append(arguments[argument].View());
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

    String FormatEmitErrorLine(std::string_view error, const DiagnosticMessages& messages)
    {
        // `경로(줄,열): ` 이 앞에 있는가. 경로에 괄호가 있을 수 있으므로 처음으로 `숫자,숫자): ` 가 오는 `(` 를 찾는다.
        std::size_t split = std::string_view::npos;
        for (std::size_t open = error.find('('); open != std::string_view::npos; open = error.find('(', open + 1))
        {
            std::size_t cursor = open + 1;
            std::size_t digits = 0;
            while (cursor < error.size() && error[cursor] >= '0' && error[cursor] <= '9')
            {
                ++cursor;
                ++digits;
            }
            if (digits == 0 || cursor >= error.size() || error[cursor] != ',')
            {
                continue;
            }
            ++cursor;
            digits = 0;
            while (cursor < error.size() && error[cursor] >= '0' && error[cursor] <= '9')
            {
                ++cursor;
                ++digits;
            }
            if (digits != 0 && error.substr(cursor, 3) == "): ")
            {
                split = cursor + 1;
                break;
            }
        }
        const std::string_view origin = split == std::string_view::npos ? std::string_view("jbroc ") : error.substr(0, split);
        const std::string_view detail = split == std::string_view::npos ? error : error.substr(split + 2);
        const String argument(detail);
        String line(origin);
        line.Append(": error JBC4001: ");
        line.Append(messages.FormatKey("jbroc.emit.unsupported", ArrayView<const String>(&argument, 1)).View());
        return line;
    }

    String FormatDiagnosticLine(std::string_view path, std::string_view text, const Diagnostic& diagnostic,
        const DiagnosticMessages& messages)
    {
        String line;
        line.Append(path);
        line.Append("(");
        line.Append(std::to_string(diagnostic.Range.Begin.Line));
        line.Append(",");
        line.Append(std::to_string(CharacterColumn(text, diagnostic.Range.Begin).Get()));
        line.Append("): error JBC");
        line.Append(std::to_string(GetDiagnosticNumber(diagnostic.Code)));
        line.Append(": ");
        line.Append(messages.Format(diagnostic).View());
        return line;
    }
}
