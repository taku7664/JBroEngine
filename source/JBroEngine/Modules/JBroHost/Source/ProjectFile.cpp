#include <JBro/Host/ProjectFile.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace JBro
{
    namespace
    {
        bool IsBlankOrComment(const String& text)
        {
            for (char character : text)
            {
                if (character == '#')
                {
                    return true;
                }
                if (character != ' ' && character != '\r')
                {
                    return false;
                }
            }
            return true;
        }

        // YAML 블록 스칼라 표시다(`|`, `|+`, `>-` …). 아래에 여러 줄이 붙는다.
        bool IsBlockScalarIndicator(const String& value)
        {
            if (value.empty() || (value[0] != '|' && value[0] != '>'))
            {
                return false;
            }
            for (std::size_t index = 1; index < value.size(); ++index)
            {
                const char character = value[index];
                if (character != '+' && character != '-'
                    && (character < '0' || character > '9'))
                {
                    return false;
                }
            }
            return true;
        }

        String Trim(const char* begin, const char* end)
        {
            while (begin < end && (*begin == ' ' || *begin == '\r'))
            {
                ++begin;
            }
            while (end > begin && (end[-1] == ' ' || end[-1] == '\r'))
            {
                --end;
            }
            return String(begin, static_cast<std::size_t>(end - begin));
        }

        // 따옴표는 벗긴다. 여는 따옴표만 있고 닫는 것이 없으면 실패로 본다.
        bool Unquote(String& value)
        {
            if (value.size() < 2)
            {
                return true;
            }
            const char first = value[0];
            if (first != '"' && first != '\'')
            {
                return true;
            }
            if (value[value.size() - 1] != first)
            {
                return false;
            }
            value = String(value.c_str() + 1, value.size() - 2);
            return true;
        }

        bool ParseBool(const String& value, bool& result)
        {
            if (value == "true")
            {
                result = true;
                return true;
            }
            if (value == "false")
            {
                result = false;
                return true;
            }
            return false;
        }

        bool ParseUInt(const String& value, std::uint32_t& result)
        {
            if (value.empty())
            {
                return false;
            }
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
            {
                return false;
            }
            result = static_cast<std::uint32_t>(parsed);
            return true;
        }

        bool ParseFloat(const String& value, float& result)
        {
            if (value.empty())
            {
                return false;
            }
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end == nullptr || *end != '\0')
            {
                return false;
            }
            result = static_cast<float>(parsed);
            return true;
        }

        bool Fail(ProjectFileError& error, std::size_t line, const char* message)
        {
            error.line = static_cast<std::uint32_t>(line);
            error.message = message;
            return false;
        }
    }

    bool ParseProjectFile(
        const char* text,
        std::size_t length,
        ProjectFile& result,
        ProjectFileError& error)
    {
        error = {};
        if (text == nullptr)
        {
            return Fail(error, 0, "no project text");
        }

        ProjectFile parsed;
        // 지금 아는 중첩 맵은 Build 하나다. 모르는 키 아래의 블록은 통째로 건너뛴다 —
        // 실제 프로젝트 파일에는 이 엔진이 아직 쓰지 않는 맵의 시퀀스(AudioBuses,
        // InputActions)가 들어 있고, 읽지 않을 것을 파싱하려다 틀리느니 지나가는 편이 낫다.
        String         currentMap;
        Array<String>* currentSequence = nullptr;
        constexpr std::size_t NotSkipping = static_cast<std::size_t>(-1);
        std::size_t    skipDeeperThan = NotSkipping;

        std::size_t lineNumber = 0;
        std::size_t cursor = 0;
        while (cursor <= length)
        {
            const std::size_t begin = cursor;
            while (cursor < length && text[cursor] != '\n')
            {
                ++cursor;
            }
            const String raw(text + begin, cursor - begin);
            ++lineNumber;
            const bool atEnd = cursor >= length;
            ++cursor;

            if (IsBlankOrComment(raw))
            {
                if (atEnd)
                {
                    break;
                }
                continue;
            }
            for (char character : raw)
            {
                if (character == '\t')
                {
                    return Fail(error, lineNumber, "tab indentation is not read");
                }
            }

            std::size_t indent = 0;
            while (indent < raw.size() && raw[indent] == ' ')
            {
                ++indent;
            }
            if ((indent % 2) != 0)
            {
                return Fail(error, lineNumber, "indentation must be a multiple of two spaces");
            }

            if (skipDeeperThan != NotSkipping && indent > skipDeeperThan)
            {
                if (atEnd)
                {
                    break;
                }
                continue;
            }
            skipDeeperThan = NotSkipping;

            const char* content = raw.c_str() + indent;
            const char* contentEnd = raw.c_str() + raw.size();
            if (indent == 0)
            {
                currentMap.clear();
                currentSequence = nullptr;
            }

            // 시퀀스 항목. 모르는 키 아래의 것은 위에서 이미 걸러졌다.
            if (content[0] == '-' && (content[1] == ' ' || content[1] == '\0'))
            {
                if (currentSequence == nullptr)
                {
                    return Fail(error, lineNumber, "a sequence item has no key above it");
                }
                String item = Trim(content + 1, contentEnd);
                if (false == Unquote(item))
                {
                    return Fail(error, lineNumber, "unterminated quoted string");
                }
                currentSequence->Add(item);
                if (atEnd)
                {
                    break;
                }
                continue;
            }

            // 빈 시퀀스는 키 아래 들여쓴 `[]` 한 줄로 쓴다.
            if (Trim(content, contentEnd) == "[]")
            {
                if (currentSequence == nullptr)
                {
                    return Fail(error, lineNumber, "an empty sequence has no key above it");
                }
                currentSequence = nullptr;
                if (atEnd)
                {
                    break;
                }
                continue;
            }

            const char* colon = std::strchr(content, ':');
            if (colon == nullptr)
            {
                return Fail(error, lineNumber, "expected `key: value`");
            }
            const String key = Trim(content, colon);
            String value = Trim(colon + 1, contentEnd);
            if (false == Unquote(value))
            {
                return Fail(error, lineNumber, "unterminated quoted string");
            }
            if (key.empty())
            {
                return Fail(error, lineNumber, "empty key");
            }

            // 여러 줄 스칼라다. 이 엔진이 읽는 키 중에는 없으므로 블록을 건너뛴다.
            if (IsBlockScalarIndicator(value))
            {
                currentSequence = nullptr;
                skipDeeperThan = indent;
                if (atEnd)
                {
                    break;
                }
                continue;
            }

            // 값이 비어 있으면 아래에 블록이 온다.
            if (value.empty())
            {
                currentSequence = nullptr;
                if (indent == 0 && key == "Build")
                {
                    currentMap = key;
                }
                else if (currentMap == "Build" && key == "BuildCanvases")
                {
                    currentSequence = &parsed.build.buildCanvases;
                }
                else
                {
                    // 이 엔진이 읽지 않는 블록이다. 더 깊은 줄을 전부 건너뛴다.
                    skipDeeperThan = indent;
                }
                if (atEnd)
                {
                    break;
                }
                continue;
            }

            bool recognized = true;
            if (currentMap == "Build")
            {
                if (key == "ProductName") { parsed.build.productName = value; }
                else if (key == "EnableWindows") { recognized = ParseBool(value, parsed.build.enableWindows); }
                else if (key == "EnableWeb") { recognized = ParseBool(value, parsed.build.enableWeb); }
                else if (key == "EnableAndroid") { recognized = ParseBool(value, parsed.build.enableAndroid); }
                else if (key == "EnableIOS") { recognized = ParseBool(value, parsed.build.enableIOS); }
                else if (key == "OutputDirectory") { parsed.build.outputDirectory = value; }
                else if (key == "StartupCanvas") { parsed.build.startupCanvas = value; }
                else if (key == "ScriptOutputLibraryPath") { parsed.build.scriptOutputLibraryPath = value; }
                // Build 의 나머지 키는 아직 쓰지 않는다. 값을 두고 지나간다.
            }
            else if (key == "Version") { recognized = ParseUInt(value, parsed.version); }
            else if (key == "RootPath") { parsed.rootPath = value; }
            else if (key == "ResolutionWidth") { recognized = ParseUInt(value, parsed.resolutionWidth); }
            else if (key == "ResolutionHeight") { recognized = ParseUInt(value, parsed.resolutionHeight); }
            else if (key == "PixelsPerUnit") { recognized = ParseFloat(value, parsed.pixelsPerUnit); }
            else if (key == "DebugModeEnabled") { recognized = ParseBool(value, parsed.debugModeEnabled); }
            else if (key == "ScriptSourceDirectory") { parsed.scriptSourceDirectory = value; }
            else if (key == "ScriptOutputLibraryPath") { parsed.scriptOutputLibraryPath = value; }
            else if (key == "LastOpenedCanvasPath") { parsed.lastOpenedCanvasPath = value; }
            // 최상위의 나머지 키도 아직 쓰지 않는다.

            if (false == recognized)
            {
                return Fail(error, lineNumber, "value does not match the type this key expects");
            }
            currentSequence = nullptr;
            if (atEnd)
            {
                break;
            }
        }

        if (parsed.version == 0)
        {
            return Fail(error, 0, "project version must not be zero");
        }
        result = parsed;
        return true;
    }

    bool LoadProjectFile(const char* path, ProjectFile& result, ProjectFileError& error)
    {
        error = {};
        if (path == nullptr || path[0] == '\0')
        {
            return Fail(error, 0, "no project path");
        }

        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        {
            return Fail(error, 0, "cannot open the project file");
        }
        String text;
        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            text.append(buffer, read);
        }
        std::fclose(file);
        return ParseProjectFile(text.c_str(), text.size(), result, error);
    }

    String ResolveScriptModulePath(const ProjectFile& project, const char* projectFilePath)
    {
        const String& relative = project.scriptOutputLibraryPath;
        if (relative.empty())
        {
            return String();
        }
        // 드라이브 문자나 루트로 시작하면 절대경로다.
        const bool absolute = relative[0] == '/' || relative[0] == '\\'
            || (relative.size() > 1 && relative[1] == ':');
        if (absolute || projectFilePath == nullptr)
        {
            return relative;
        }

        String directory(projectFilePath);
        const std::size_t slash = directory.find_last_of("/\\");
        if (slash == String::npos)
        {
            return relative;
        }
        directory.resize(slash + 1);
        directory.append(relative.c_str(), relative.size());
        return directory;
    }
}
