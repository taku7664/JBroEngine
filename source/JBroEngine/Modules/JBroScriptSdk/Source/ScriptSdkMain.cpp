#include <JBro/Types/Types.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

// 사용자 스크립트 프로젝트가 include 하는 헤더 사본(스크립트 SDK)을 만든다.
// 스크립트가 쓰는 입구 헤더에서 include 로 닿는 것만 옮기고 주석을 모두 지운다. 엔진 개발자의 주석(결정 번호·계획서 참조)은
// 엔진 소스에 남고 사용자에게는 가지 않는다. 사본의 줄은 원본과 달라지므로, API 목록(JBroScriptApiCatalog2D)이 정의 위치를
// 사본 기준으로 옮길 수 있게 원본 줄 → 사본 줄 대응표를 함께 낸다.
//
// 쓰기: JBroScriptSdk.exe <엔진 루트(source/JBroEngine)> <출력 폴더>
// 바뀐 파일만 다시 쓴다 - 엔진을 다시 빌드할 때마다 사용자 스크립트가 모두 다시 컴파일되지 않게.

namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    fs::path Utf8Path(const String& text)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    String PathText(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(std::string(reinterpret_cast<const char*>(text.data()), text.size()));
    }

    String ReadFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("cannot read " + PathText(path).Std());
        }
        return String(std::string(std::istreambuf_iterator<char>(input), {}));
    }

    // 내용이 같으면 쓰지 않는다. 시각이 그대로여야 사용자 스크립트가 다시 컴파일되지 않는다.
    void WriteIfChanged(const fs::path& path, const String& text)
    {
        std::error_code error;
        if (fs::exists(path, error) && ReadFile(path) == text)
        {
            return;
        }
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
        {
            throw std::runtime_error("cannot write " + PathText(path).Std());
        }
    }

    Bool IsIdentifierChar(char character)
    {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '_';
    }

    // 글자 앞의 식별자가 리터럴 접두어(L·u·U·u8)인가. `1'000` 같은 자릿수 구분자와 가른다.
    Bool IsLiteralPrefix(const String& text, std::size_t quote)
    {
        std::size_t start = quote;
        while (start > 0 && IsIdentifierChar(text[start - 1]))
        {
            --start;
        }
        const std::string_view prefix(text.data() + start, quote - start);
        return prefix.empty() || prefix == "L" || prefix == "u" || prefix == "U" || prefix == "u8"
            || prefix == "R" || prefix == "LR" || prefix == "uR" || prefix == "UR" || prefix == "u8R";
    }

    struct Stripped
    {
        String Text;
        // 원본 줄(0 부터) → 사본 줄(1 부터). 0 은 지운 줄이다.
        Array<UInt32> Lines;
        // 사본 줄(0 부터)이 시작하는 바이트 위치. BOM 을 포함한 파일 기준이다.
        Array<UInt32> Starts;
    };

    // 주석을 지운다. 문자열·글자·원시 문자열 리터럴 안의 `//` 는 건드리지 않는다.
    // 주석만 있던 줄은 없애고, 코드 뒤의 주석은 떼며, 코드 앞에 놓인 블록 주석은 같은 폭의 공백으로 바꿔 열을 지킨다.
    // 빈 줄은 한 줄로 접는다.
    Stripped StripComments(const String& source)
    {
        const Bool bom = source.StartsWith("\xEF\xBB\xBF");
        String text = bom ? source.Substr(3) : source;
        text.ReplaceAll("\r\n", "\n");

        // 주석 글자를 0x01 로 가린다. 줄바꿈은 남긴다.
        constexpr char Mask = '\x01';
        enum class Mode { Code, Line, Block, String, Character, Raw };
        Mode mode = Mode::Code;
        String rawEnd;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const char character = text[index];
            const char next = index + 1 < text.size() ? text[index + 1] : '\0';
            switch (mode)
            {
            case Mode::Code:
                if (character == '/' && next == '/')
                {
                    mode = Mode::Line;
                    text[index] = Mask;
                }
                else if (character == '/' && next == '*')
                {
                    mode = Mode::Block;
                    text[index] = Mask;
                    text[index + 1] = Mask;
                    ++index;
                }
                else if (character == '"' && index > 0 && text[index - 1] == 'R' && IsLiteralPrefix(text, index))
                {
                    const std::size_t open = text.find('(', index);
                    if (open == String::npos)
                    {
                        throw std::runtime_error("raw string without an opening parenthesis");
                    }
                    rawEnd = String(")") + text.Substr(index + 1, open - index - 1) + "\"";
                    mode = Mode::Raw;
                    index = open;
                }
                else if (character == '"')
                {
                    mode = Mode::String;
                }
                else if (character == '\'' && (index == 0 || !IsIdentifierChar(text[index - 1]) || IsLiteralPrefix(text, index)))
                {
                    mode = Mode::Character;
                }
                break;
            case Mode::Line:
                if (character == '\n')
                {
                    mode = Mode::Code;
                }
                else
                {
                    text[index] = Mask;
                }
                break;
            case Mode::Block:
                if (character == '*' && next == '/')
                {
                    text[index] = Mask;
                    text[index + 1] = Mask;
                    ++index;
                    mode = Mode::Code;
                }
                else if (character != '\n')
                {
                    text[index] = Mask;
                }
                break;
            case Mode::String:
            case Mode::Character:
                if (character == '\\')
                {
                    ++index;
                }
                else if ((mode == Mode::String && character == '"') || (mode == Mode::Character && character == '\''))
                {
                    mode = Mode::Code;
                }
                else if (character == '\n')
                {
                    throw std::runtime_error("unterminated literal");
                }
                break;
            case Mode::Raw:
                if (text.View().substr(index, rawEnd.size()) == rawEnd.View())
                {
                    index += rawEnd.size() - 1;
                    mode = Mode::Code;
                }
                break;
            }
        }

        Stripped result;
        const String prefix = bom ? String("\xEF\xBB\xBF") : String();
        result.Text = prefix;
        Bool lastBlank = true;
        Bool pendingBlank = false;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = text.find('\n', start);
            const Bool last = end == String::npos;
            if (last)
            {
                end = text.size();
            }
            String line = text.Substr(start, end - start);
            const Bool hadText = line.find_first_not_of(" \t") != String::npos;
            const std::size_t code = line.find_last_not_of(std::string(" \t") + Mask);
            if (code == String::npos)
            {
                line.clear();
            }
            else
            {
                line.resize(code + 1);
                for (char& character : line)
                {
                    if (character == Mask)
                    {
                        character = ' ';
                    }
                }
                line.TrimRight();
            }

            UInt32 copyLine = 0;
            if (line.IsNotEmpty())
            {
                if (pendingBlank)
                {
                    result.Starts.Add(static_cast<UInt32>(result.Text.size()));
                    result.Text.Append("\r\n");
                    pendingBlank = false;
                }
                result.Starts.Add(static_cast<UInt32>(result.Text.size()));
                copyLine = static_cast<UInt32>(result.Starts.Size());
                result.Text.Append(line.View()).Append("\r\n");
                lastBlank = false;
            }
            else if (!hadText && !lastBlank)
            {
                // 원래 빈 줄이다. 다음 코드 줄이 올 때만 하나를 남긴다 - 연달은 빈 줄과 끝의 빈 줄이 남지 않는다.
                pendingBlank = true;
                lastBlank = true;
            }
            result.Lines.Add(copyLine);
            if (last)
            {
                break;
            }
            start = end + 1;
        }
        // 첫 줄의 열은 원본처럼 BOM 을 센다(시험의 `CheckDefinition` 과 같은 셈). 그래서 첫 줄의 시작은 0 이다.
        if (!result.Starts.IsEmpty())
        {
            result.Starts[0] = 0;
        }
        return result;
    }

    struct IncludeRoot
    {
        fs::path Directory;
        // 대응표의 열쇠로 쓰는 `source/` 기준 경로의 머리. API 목록의 정의 표가 이 모양으로 적는다.
        String Key;
    };

    struct Header
    {
        String Relative;
        fs::path Source;
        String Key;
    };

    Array<IncludeRoot> RootsFor(const fs::path& engine, const char* dimension)
    {
        const char* const modules[] = {
            "JBroCore", "JBroRuntime", "JBroAssetTypes", "JBroAudioTypes", "JBroInputTypes",
            "JBroSaveTypes", "JBroLocalizationTypes",
        };
        // `JBro.Common.props` 의 `JBroScriptIncludes` 와 같은 순서다.
        Array<IncludeRoot> roots;
        for (const char* module : modules)
        {
            roots.Add({engine / "Modules" / module / "Include", String("JBroEngine/Modules/") + module + "/Include/"});
        }
        roots.Add({engine / ".." / "JBroNetwork" / "JBroNetwork" / "Include", String("JBroNetwork/JBroNetwork/Include/")});
        const String framework = String("JBroFramework") + dimension;
        roots.Add({engine / "Modules" / framework.Std() / "Include", String("JBroEngine/Modules/") + framework + "/Include/"});
        return roots;
    }

    Bool Find(const Array<IncludeRoot>& roots, const String& relative, Header& found)
    {
        for (const IncludeRoot& root : roots)
        {
            const fs::path candidate = root.Directory / Utf8Path(relative);
            std::error_code error;
            if (fs::is_regular_file(candidate, error))
            {
                found = {relative, candidate, root.Key + relative};
                return true;
            }
        }
        return false;
    }

    String Normalize(const String& relative)
    {
        return PathText(Utf8Path(relative).lexically_normal());
    }

    // `#include <...>` 와 `#include "..."` 를 읽는다. 조건부 include 도 따라간다 - 넘쳐도 해가 없고, 모자라면 사용자 빌드가 깨진다.
    Array<String> ReadIncludes(const String& text, const String& relative)
    {
        Array<String> includes;
        std::size_t start = 0;
        while (start < text.size())
        {
            std::size_t end = text.find('\n', start);
            if (end == String::npos)
            {
                end = text.size();
            }
            const String line = text.Substr(start, end - start).Trimmed();
            start = end + 1;
            if (!line.StartsWith("#"))
            {
                continue;
            }
            String directive = line.Substr(1).Trimmed();
            if (!directive.StartsWith("include"))
            {
                continue;
            }
            directive = directive.Substr(7).Trimmed();
            if (directive.empty())
            {
                continue;
            }
            const char close = directive[0] == '<' ? '>' : (directive[0] == '"' ? '"' : '\0');
            const std::size_t closing = close == '\0' ? String::npos : directive.find(close, 1);
            if (closing == String::npos)
            {
                continue;
            }
            const String name = directive.Substr(1, closing - 1);
            if (close == '"')
            {
                const std::size_t slash = relative.rfind('/');
                includes.Add(Normalize((slash == String::npos ? String() : relative.Substr(0, slash + 1)) + name));
                includes.Add(name);
            }
            else
            {
                includes.Add(name);
            }
        }
        return includes;
    }

    // 스크립트 대상 빌드에서 `#error` 로 막는 헤더면 그 `#error` 줄을 돌려준다. 데이터 헤더를 잘못 include 한 사용자가
    // "파일이 없다" 대신 무엇을 써야 하는지를 보게, 사본에는 이 줄만 남긴다.
    String ScriptTargetError(const String& text)
    {
        const std::size_t guard = text.find("#if defined(JBRO_SCRIPT_TARGET)");
        if (guard == String::npos)
        {
            return String();
        }
        const std::size_t error = text.find("#error", guard);
        const std::size_t endif = text.find("#endif", guard);
        if (error == String::npos || (endif != String::npos && endif < error))
        {
            return String();
        }
        std::size_t end = text.find('\n', error);
        String line = text.Substr(error, end == String::npos ? String::npos : end - error);
        line.TrimRight();
        return line;
    }

    String CppString(const String& text)
    {
        String quoted = "\"";
        for (const char character : text)
        {
            if (character == '"' || character == '\\')
            {
                quoted.push_back('\\');
            }
            quoted.push_back(character);
        }
        quoted.push_back('"');
        return quoted;
    }

    void AppendNumbers(String& out, const Array<UInt32>& values)
    {
        for (std::size_t index = 0; index < values.Size(); ++index)
        {
            out.Append(index % 24 == 0 ? "\n    " : "");
            out.Append(std::to_string(values[index])).Append(",");
        }
    }

    void RemoveStale(const fs::path& directory, const Array<String>& written)
    {
        std::error_code error;
        if (!fs::exists(directory, error))
        {
            return;
        }
        Array<fs::path> stale;
        for (const auto& entry : fs::recursive_directory_iterator(directory))
        {
            if (entry.is_regular_file() && !written.Contains(PathText(fs::relative(entry.path(), directory))))
            {
                stale.Add(entry.path());
            }
        }
        for (const fs::path& path : stale)
        {
            fs::remove(path);
        }
        Array<fs::path> folders;
        for (const auto& entry : fs::recursive_directory_iterator(directory))
        {
            if (entry.is_directory())
            {
                folders.Add(entry.path());
            }
        }
        for (std::size_t index = folders.Size(); index > 0; --index)
        {
            if (fs::is_empty(folders[index - 1], error))
            {
                fs::remove(folders[index - 1], error);
            }
        }
    }

    void BuildDimension(const fs::path& engine, const fs::path& output, const char* dimension, Bool writeLineMap)
    {
        const Array<IncludeRoot> roots = RootsFor(engine, dimension);
        // 스크립트가 쓰는 입구다. C++ 스크립트의 프렐류드와 모듈 진입점, JBroScript 가 내는 C++ 이 include 하는 것,
        // 그리고 Core 의 공개 값 타입(`JBro/Types`) 전부.
        Array<String> pending = {
            "JBro/ScriptAPI.h",
            String("JBro/Framework") + dimension + "/Scripting/ScriptModule.h",
            "JBro/Runtime/ScriptGuard.h",
            "JBro/Runtime/ScriptInitialization.h",
            "JBro/Runtime/ScriptStatics.h",
            "JBro/Reflection/EnumDescriptor.h",
            "JBro/Reflection/Math3DReflection.h",
        };
        for (const auto& entry : fs::directory_iterator(engine / "Modules" / "JBroCore" / "Include" / "JBro" / "Types"))
        {
            if (entry.path().extension() == ".h")
            {
                pending.Add(String("JBro/Types/") + PathText(entry.path().filename()));
            }
        }

        Array<Header> headers;
        Array<String> visited;
        while (!pending.IsEmpty())
        {
            const String relative = pending.Pop();
            if (visited.Contains(relative))
            {
                continue;
            }
            visited.Add(relative);
            Header header;
            if (!Find(roots, relative, header))
            {
                if (relative.StartsWith("JBro/"))
                {
                    throw std::runtime_error("a script header includes <" + relative.Std() + "> which no script include folder has");
                }
                continue;
            }
            headers.Add(header);
            for (const String& include : ReadIncludes(ReadFile(header.Source), relative))
            {
                pending.Add(include);
            }
        }

        // JBroScript 에만 보이는 값 타입(`InputCapture`)은 API 목록 모듈이 정의한다. 사용자가 정의로 가면 이 파일이 열리므로 사본에 함께 둔다.
        if (writeLineMap)
        {
            headers.Add({"JBro/ScriptApiCatalog2D/ProjectedValues.h",
                engine / "Modules" / "JBroScriptApiCatalog2D" / "Source" / "ProjectedValues.h",
                "JBroEngine/Modules/JBroScriptApiCatalog2D/Source/ProjectedValues.h"});
        }

        const fs::path include = output / dimension / "Include";
        Array<String> written;
        String lineMap;
        String table;
        UInt32 index = 0;
        for (const Header& header : headers)
        {
            const Stripped stripped = StripComments(ReadFile(header.Source));
            WriteIfChanged(include / Utf8Path(header.Relative), stripped.Text);
            written.Add(header.Relative);
            if (writeLineMap)
            {
                const String id = std::to_string(index++);
                lineMap.Append("inline constexpr UInt32 Lines").Append(id.View()).Append("[] = {");
                AppendNumbers(lineMap, stripped.Lines);
                lineMap.Append("\n};\ninline constexpr UInt32 Starts").Append(id.View()).Append("[] = {");
                AppendNumbers(lineMap, stripped.Starts);
                lineMap.Append("\n};\n");
                table.Append("    {").Append(CppString(header.Key).View()).Append(", ").Append(CppString(header.Relative).View())
                    .Append(", Lines").Append(id.View()).Append(", ").Append(std::to_string(stripped.Lines.Size()))
                    .Append(", Starts").Append(id.View()).Append(", ").Append(std::to_string(stripped.Starts.Size())).Append("},\n");
            }
        }

        // 입구에서 닿지 않지만 스크립트 대상 빌드를 `#error` 로 막는 헤더는 그 줄만 둔다.
        for (const IncludeRoot& root : roots)
        {
            for (const auto& entry : fs::recursive_directory_iterator(root.Directory))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".h")
                {
                    continue;
                }
                const String relative = PathText(fs::relative(entry.path(), root.Directory));
                if (written.Contains(relative))
                {
                    continue;
                }
                const String error = ScriptTargetError(ReadFile(entry.path()));
                if (error.IsNotEmpty())
                {
                    WriteIfChanged(include / Utf8Path(relative), String("#pragma once\r\n") + error + "\r\n");
                    written.Add(relative);
                }
            }
        }
        RemoveStale(include, written);

        if (writeLineMap)
        {
            String out = "// JBroScriptSdk 가 만든 파일이다. 고치지 않는다.\r\n"
                "// 원본 헤더의 줄(0 부터) -> 스크립트 SDK 사본의 줄(1 부터, 0 은 지운 줄)과 사본 줄의 시작 바이트.\r\n"
                "namespace JBro::ScriptApiCatalog2D::Internal::ScriptSdk\r\n{\r\n";
            lineMap.ReplaceAll("\n", "\r\n");
            table.ReplaceAll("\n", "\r\n");
            out.Append(lineMap.View());
            out.Append("struct HeaderMap { const char* Source; const char* Sdk; const UInt32* Lines; UInt32 LineCount; const UInt32* Starts; UInt32 StartCount; };\r\n");
            out.Append("inline constexpr HeaderMap Headers[] = {\r\n").Append(table.View()).Append("};\r\n}\r\n");
            WriteIfChanged(output / (std::string("ScriptSdkLineMap") + dimension + ".inc"), out);
        }
        std::printf("JBroScriptSdk: %s %u headers -> %s\n", dimension, static_cast<unsigned>(written.Size()), PathText(include).c_str());
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: JBroScriptSdk <engine root> <output folder>\n");
        return 2;
    }
    try
    {
        const fs::path engine = fs::path(argv[1]).lexically_normal();
        const fs::path output = fs::path(argv[2]).lexically_normal();
        BuildDimension(engine, output, "2D", true);
        BuildDimension(engine, output, "3D", false);
        // MSBuild 의 증분 빌드가 보는 표식이다. 사본은 바뀐 것만 쓰므로 표식이 따로 있어야 한다.
        std::ofstream(output / ".stamp", std::ios::trunc) << "ok\n";
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "JBroScriptSdk: error: %s\n", error.what());
        return 1;
    }
    return 0;
}
