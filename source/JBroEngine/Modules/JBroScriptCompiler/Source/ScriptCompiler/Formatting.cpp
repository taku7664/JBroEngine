#include <JBro/ScriptCompiler/Formatting.h>
#include <JBro/ScriptCompiler/Parser.h>
#include <JBro/Types/Int.h>

#include <algorithm>
#include <string_view>

namespace JBro::ScriptCompiler
{
    namespace
    {
        Bool White(char ch)
        {
            return ch == ' ' || ch == '\t';
        }

        void ScanLine(std::string_view text, Int32& braces, Int32& parentheses)
        {
            Bool string = false;
            Bool escaped = false;
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const char ch = text[index];
                if (string)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (ch == '\\')
                    {
                        escaped = true;
                    }
                    else if (ch == '"')
                    {
                        string = false;
                    }
                    continue;
                }
                if (ch == '/' && index + 1 < text.size() && text[index + 1] == '/')
                {
                    break;
                }
                if (ch == '"')
                {
                    string = true;
                }
                else if (ch == '{')
                {
                    ++braces;
                }
                else if (ch == '}')
                {
                    braces = std::max(Int32(0), braces - 1);
                }
                else if (ch == '(' || ch == '[')
                {
                    ++parentheses;
                }
                else if (ch == ')' || ch == ']')
                {
                    parentheses = std::max(Int32(0), parentheses - 1);
                }
            }
        }
    }

    Bool FormatSource(const SourceText& source, UInt32 tabSize, Bool insertSpaces, String& formatted)
    {
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        (void)tree;
        if (diagnostics.HasErrors())
        {
            return false;
        }
        String result;
        std::string_view text = source.GetText();
        if (text.starts_with("\xEF\xBB\xBF"))
        {
            result.Append(text.substr(0, 3));
            text.remove_prefix(3);
        }
        const UInt32 width = std::clamp(tabSize, UInt32(1), UInt32(16));
        Int32 braces = 0;
        Int32 parentheses = 0;
        std::size_t begin = 0;
        while (begin < text.size())
        {
            const std::size_t newline = text.find('\n', begin);
            const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
            const Bool crlf = end > begin && text[end - 1] == '\r';
            std::string_view line = text.substr(begin, end - begin - (crlf ? 1 : 0));
            while (false == line.empty() && White(line.front()))
            {
                line.remove_prefix(1);
            }
            Int32 closing = 0;
            for (const char ch : line)
            {
                if (ch == '}')
                {
                    ++closing;
                }
                else if (false == White(ch))
                {
                    break;
                }
            }
            Int32 indentation = std::max(Int32(0), braces - closing);
            if (parentheses > 0 && false == line.empty() && line.front() != ')' && line.front() != ']')
            {
                ++indentation;
            }
            if (false == line.empty())
            {
                const Int32 count = insertSpaces ? indentation * static_cast<Int32>(width) : indentation;
                for (Int32 index = 0; index < count; ++index)
                {
                    result.Append(insertSpaces ? " " : "\t");
                }
                result.Append(line);
            }
            ScanLine(line, braces, parentheses);
            if (newline != std::string_view::npos)
            {
                result.Append(crlf ? "\r\n" : "\n");
                begin = newline + 1;
            }
            else
            {
                begin = text.size();
            }
        }
        formatted = std::move(result);
        return true;
    }
}
