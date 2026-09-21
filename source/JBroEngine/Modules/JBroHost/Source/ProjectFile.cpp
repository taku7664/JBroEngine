#include <JBro/Host/ProjectFile.h>

#include <JBro/Platform/Platform.h>

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

        bool ParseTextureFilter(const String& value, TextureFilter& result)
        {
            if (value == "Nearest")
            {
                result = TextureFilter::Nearest;
                return true;
            }
            if (value == "Linear")
            {
                result = TextureFilter::Linear;
                return true;
            }
            return false;
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

        // `Framework` 가 받는 값이다. 사람이 손으로 적는 자리라 대소문자는 가리지 않는다.
        bool ParseFrameworkKind(const String& value, FrameworkKind& result)
        {
            if (value == "2D" || value == "2d")
            {
                result = FrameworkKind::Framework2D;
                return true;
            }
            if (value == "3D" || value == "3d")
            {
                result = FrameworkKind::Framework3D;
                return true;
            }
            return false;
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
        // 두 키는 있어야 한다(D-99). 값이 비어 있는 것도 없는 것으로 본다 -
        // 런처가 빈 엔진 버전으로는 어느 설치를 띄울지 고를 수 없다.
        bool           sawEngineVersion = false;
        bool           sawFramework = false;
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
            // 값이 있는지는 **따옴표를 벗기기 전에** 본다. `Key: ""` 는 빈 문자열이라는
            // 값이고 `Key:` 는 아래에 블록이 온다는 뜻인데, 먼저 벗기면 둘이 같아진다.
            // 그러면 명시적으로 비운 키가 통째로 무시되고, 뒤따르는 줄까지 건너뛴다.
            const bool hasValue = false == value.empty();
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
            if (false == hasValue)
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
                else if (indent == 0 && key == "AssetIgnorePatterns")
                {
                    parsed.assetIgnorePatterns.Clear();
                    currentSequence = &parsed.assetIgnorePatterns;
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
            else if (key == "EngineVersion")
            {
                parsed.engineVersion = value;
                sawEngineVersion = false == value.empty();
            }
            else if (key == "Framework")
            {
                recognized = ParseFrameworkKind(value, parsed.framework);
                sawFramework = recognized;
            }
            else if (key == "RootPath") { parsed.rootPath = value; }
            else if (key == "ResolutionWidth") { recognized = ParseUInt(value, parsed.resolutionWidth); }
            else if (key == "ResolutionHeight") { recognized = ParseUInt(value, parsed.resolutionHeight); }
            else if (key == "TextureFilter")
            {
                if (false == ParseTextureFilter(value, parsed.textureFilter))
                {
                    return Fail(error, lineNumber,
                        "TextureFilter is Nearest or Linear; Default belongs to a texture's import options");
                }
            }
            else if (key == "DebugModeEnabled") { recognized = ParseBool(value, parsed.debugModeEnabled); }
            else if (key == "ScriptSourceDirectory") { parsed.scriptSourceDirectory = value; }
            else if (key == "ScriptOutputLibraryPath") { parsed.scriptOutputLibraryPath = value; }
            else if (key == "LastOpenedCanvasPath") { parsed.lastOpenedCanvasPath = value; }
            else if (key == "EditorLocale") { parsed.editorLocale = value; }
            else if (key == "CanvasViewCameraX")
            {
                if (false == ParseFloat(value, parsed.canvasViewCameraX))
                {
                    return Fail(error, lineNumber, "CanvasViewCameraX must be a number");
                }
            }
            else if (key == "CanvasViewCameraY")
            {
                if (false == ParseFloat(value, parsed.canvasViewCameraY))
                {
                    return Fail(error, lineNumber, "CanvasViewCameraY must be a number");
                }
            }
            else if (key == "CanvasViewCameraSize")
            {
                if (false == ParseFloat(value, parsed.canvasViewCameraSize))
                {
                    return Fail(error, lineNumber, "CanvasViewCameraSize must be a number");
                }
            }
            else if (key == "AssetDirectory") { parsed.assetDirectory = value; }
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
        if (false == sawEngineVersion)
        {
            return Fail(error, 0, "the project must say which engine version it opens with (EngineVersion)");
        }
        if (false == sawFramework)
        {
            return Fail(error, 0, "the project must say which framework it runs on (Framework: 2D or 3D)");
        }
        result = parsed;
        return true;
    }

    bool LoadProjectFile(IPlatform& platform, const char* utf8Path, ProjectFile& result, ProjectFileError& error)
    {
        error = {};
        if (utf8Path == nullptr || utf8Path[0] == '\0')
        {
            return Fail(error, 0, "no project path");
        }
        // 파일은 플랫폼이 연다(D-112). `fopen` 은 UTF-8 경로를 ANSI 로 읽어 한글 폴더에서 조용히 실패했다.
        Array<std::byte> text;
        if (false == platform.ReadWholeFile(utf8Path, text))
        {
            return Fail(error, 0, "cannot open the project file");
        }
        return ParseProjectFile(reinterpret_cast<const char*>(text.Data()), text.Size(), result, error);
    }

    namespace
    {
        // 소수를 글자로. **짧게 적되 값은 지킨다** - `%g` 는 자리를 아끼고, 9 자리면
        // float 가 왕복해도 같은 값으로 돌아온다.
        String FormatFloat(float value)
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
            return String(buffer);
        }

        // 최상위 키 하나의 지금 값을 글자로. 아는 키가 아니면 거짓이다.
        bool TopLevelValue(const ProjectFile& project, const String& key, String& value)
        {
            char number[32] = {};
            if (key == "Version")
            {
                std::snprintf(number, sizeof(number), "%u", project.version);
                value = number;
            }
            else if (key == "EngineVersion") { value = project.engineVersion; }
            else if (key == "Framework")
            {
                // 파일에 적히는 값은 `2D` / `3D` 다. 열거 이름이 아니다 -
                // 읽는 쪽(`ParseFrameworkKind`)이 그렇게 읽는다.
                value = project.framework == FrameworkKind::Framework3D ? "3D" : "2D";
            }
            else if (key == "RootPath") { value = project.rootPath; }
            else if (key == "ResolutionWidth")
            {
                std::snprintf(number, sizeof(number), "%u", project.resolutionWidth);
                value = number;
            }
            else if (key == "ResolutionHeight")
            {
                std::snprintf(number, sizeof(number), "%u", project.resolutionHeight);
                value = number;
            }
            else if (key == "TextureFilter")
            {
                value = project.textureFilter == TextureFilter::Linear ? "Linear" : "Nearest";
            }
            else if (key == "DebugModeEnabled")
            {
                value = project.debugModeEnabled ? "true" : "false";
            }
            else if (key == "ScriptSourceDirectory") { value = project.scriptSourceDirectory; }
            else if (key == "ScriptOutputLibraryPath") { value = project.scriptOutputLibraryPath; }
            else if (key == "LastOpenedCanvasPath") { value = project.lastOpenedCanvasPath; }
            else if (key == "EditorLocale") { value = project.editorLocale; }
            else if (key == "CanvasViewCameraX") { value = FormatFloat(project.canvasViewCameraX); }
            else if (key == "CanvasViewCameraY") { value = FormatFloat(project.canvasViewCameraY); }
            else if (key == "CanvasViewCameraSize")
            {
                value = FormatFloat(project.canvasViewCameraSize);
            }
            else if (key == "AssetDirectory") { value = project.assetDirectory; }
            else
            {
                return false;
            }
            return true;
        }

        bool BuildValue(const ProjectFile& project, const String& key, String& value)
        {
            if (key == "ProductName") { value = project.build.productName; }
            else if (key == "EnableWindows") { value = project.build.enableWindows ? "true" : "false"; }
            else if (key == "EnableWeb") { value = project.build.enableWeb ? "true" : "false"; }
            else if (key == "EnableAndroid") { value = project.build.enableAndroid ? "true" : "false"; }
            else if (key == "EnableIOS") { value = project.build.enableIOS ? "true" : "false"; }
            else if (key == "OutputDirectory") { value = project.build.outputDirectory; }
            else if (key == "StartupCanvas") { value = project.build.startupCanvas; }
            else if (key == "ScriptOutputLibraryPath") { value = project.build.scriptOutputLibraryPath; }
            else
            {
                return false;
            }
            return true;
        }

        // 아는 최상위 키의 차례다. 없던 키를 더할 때 이 차례로 붙는다.
        const char* const TopLevelKeys[] = {
            "Version", "EngineVersion", "Framework", "RootPath",
            "ResolutionWidth", "ResolutionHeight", "TextureFilter", "DebugModeEnabled",
            "ScriptSourceDirectory", "ScriptOutputLibraryPath", "LastOpenedCanvasPath",
            "AssetDirectory", "EditorLocale",
            "CanvasViewCameraX", "CanvasViewCameraY", "CanvasViewCameraSize"};
        const char* const BuildKeys[] = {
            "ProductName", "EnableWindows", "EnableWeb", "EnableAndroid", "EnableIOS",
            "OutputDirectory", "StartupCanvas", "ScriptOutputLibraryPath"};

        // `  Key: value` 에서 들여쓰기·키·값을 가른다. 값이 비어 있으면(블록·시퀀스의 머리)
        // `hasValue` 가 거짓이다.
        bool SplitLine(const String& line, std::size_t& indent, String& key, bool& hasValue)
        {
            indent = 0;
            while (indent < line.size() && line[indent] == ' ')
            {
                ++indent;
            }
            const std::size_t colon = line.find(':', indent);
            if (colon == String::npos)
            {
                return false;
            }
            key.assign(line.c_str() + indent, colon - indent);
            if (key.empty())
            {
                return false;
            }
            std::size_t at = colon + 1;
            while (at < line.size() && (line[at] == ' ' || line[at] == '\r'))
            {
                ++at;
            }
            hasValue = at < line.size();
            return true;
        }

        void AppendLine(String& out, const String& line)
        {
            out.append(line.c_str(), line.size());
            out.append("\n", 1);
        }

        void AppendPair(String& out, const char* indent, const String& key, const String& value)
        {
            String line(indent);
            line.append(key.c_str(), key.size());
            line.append(": ", 2);
            line.append(value.c_str(), value.size());
            AppendLine(out, line);
        }
    }

    bool WriteProjectFileText(
        const ProjectFile& project,
        const char* originalText,
        std::size_t originalLength,
        String& result,
        ProjectFileError& error)
    {
        error = ProjectFileError{};
        result.clear();
        if (originalText == nullptr)
        {
            return Fail(error, 0, "there is no project text to rewrite");
        }

        // 원문의 줄을 타고 가며 **아는 키의 값만** 바꾼다. 나머지 줄은 그대로 옮긴다 -
        // 주석도, 우리가 모르는 키도, 시퀀스도 그 자리에 남는다.
        Array<bool> wroteTopLevel;
        wroteTopLevel.Resize(sizeof(TopLevelKeys) / sizeof(TopLevelKeys[0]));
        Array<bool> wroteBuild;
        wroteBuild.Resize(sizeof(BuildKeys) / sizeof(BuildKeys[0]));
        for (std::size_t index = 0; index < wroteTopLevel.Size(); ++index)
        {
            wroteTopLevel[index] = false;
        }
        for (std::size_t index = 0; index < wroteBuild.Size(); ++index)
        {
            wroteBuild[index] = false;
        }

        bool inBuild = false;
        bool sawBuild = false;
        // `Build:` 블록이 끝나는 자리. 없던 키를 그 끝에 더한다.
        std::size_t buildEnd = String::npos;

        std::size_t at = 0;
        while (at <= originalLength)
        {
            std::size_t stop = at;
            while (stop < originalLength && originalText[stop] != '\n')
            {
                ++stop;
            }
            String line(originalText + at, stop - at);
            while (false == line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            std::size_t indent = 0;
            String key;
            bool hasValue = false;
            const bool pair = false == IsBlankOrComment(line)
                && SplitLine(line, indent, key, hasValue);

            if (pair && indent == 0)
            {
                inBuild = key == "Build";
                if (inBuild)
                {
                    sawBuild = true;
                }
            }

            String value;
            bool replaced = false;
            if (pair && hasValue)
            {
                if (indent == 0 && TopLevelValue(project, key, value))
                {
                    AppendPair(result, "", key, value);
                    replaced = true;
                    for (std::size_t index = 0; index < wroteTopLevel.Size(); ++index)
                    {
                        if (key == TopLevelKeys[index])
                        {
                            wroteTopLevel[index] = true;
                        }
                    }
                }
                else if (indent == 2 && inBuild && BuildValue(project, key, value))
                {
                    AppendPair(result, "  ", key, value);
                    replaced = true;
                    for (std::size_t index = 0; index < wroteBuild.Size(); ++index)
                    {
                        if (key == BuildKeys[index])
                        {
                            wroteBuild[index] = true;
                        }
                    }
                }
            }
            if (false == replaced)
            {
                AppendLine(result, line);
            }
            if (inBuild)
            {
                buildEnd = result.size();
            }

            if (stop >= originalLength)
            {
                break;
            }
            at = stop + 1;
        }

        // `Build:` 아래에 없던 키를 그 블록 끝에 끼운다. 블록이 아예 없으면 뒤에서 만든다.
        if (sawBuild && buildEnd != String::npos)
        {
            String added;
            for (std::size_t index = 0; index < wroteBuild.Size(); ++index)
            {
                if (wroteBuild[index])
                {
                    continue;
                }
                String value;
                const String key(BuildKeys[index]);
                if (BuildValue(project, key, value))
                {
                    AppendPair(added, "  ", key, value);
                }
            }
            if (false == added.empty())
            {
                result.insert(buildEnd, added);
            }
        }

        // 없던 최상위 키를 맨 뒤에 더한다.
        for (std::size_t index = 0; index < wroteTopLevel.Size(); ++index)
        {
            if (wroteTopLevel[index])
            {
                continue;
            }
            String value;
            const String key(TopLevelKeys[index]);
            if (TopLevelValue(project, key, value))
            {
                AppendPair(result, "", key, value);
            }
        }
        if (false == sawBuild)
        {
            AppendLine(result, String("Build:"));
            for (std::size_t index = 0; index < wroteBuild.Size(); ++index)
            {
                String value;
                const String key(BuildKeys[index]);
                if (BuildValue(project, key, value))
                {
                    AppendPair(result, "  ", key, value);
                }
            }
        }

        // **쓴 것을 도로 읽어 본다.** 읽히지 않는 글자를 파일에 남기면 그 프로젝트는
        // 다음에 열리지 않는다 - 값 안의 따옴표나 콜론 하나가 그렇게 만든다.
        ProjectFile roundTrip;
        ProjectFileError check;
        if (false == ParseProjectFile(result.c_str(), result.size(), roundTrip, check))
        {
            result.clear();
            return Fail(error, check.line,
                "the project file this would write cannot be read back");
        }
        return true;
    }

    bool SaveProjectFile(IPlatform& platform, const char* utf8Path, const ProjectFile& project,
        ProjectFileError& error)
    {
        error = ProjectFileError{};
        if (utf8Path == nullptr || utf8Path[0] == '\0')
        {
            return Fail(error, 0, "no path was given");
        }
        Array<std::byte> original;
        if (false == platform.ReadWholeFile(utf8Path, original))
        {
            return Fail(error, 0, "cannot open the project file");
        }
        String text;
        if (false == WriteProjectFileText(project,
                reinterpret_cast<const char*>(original.Data()), original.Size(), text, error))
        {
            return false;
        }

        // **바꿔치기다**(D-124). 쓰다 만 파일로 프로젝트를 잃지 않는다.
        String temporary(utf8Path);
        temporary.append(".tmp", 4);
        const JArrayView<std::byte> bytes{
            reinterpret_cast<const std::byte*>(text.c_str()),
            static_cast<std::uint32_t>(text.size())};
        if (false == platform.WriteWholeFile(temporary.c_str(), bytes))
        {
            return Fail(error, 0, "the project file could not be written");
        }
        if (false == platform.MoveFileTo(temporary.c_str(), utf8Path))
        {
            return Fail(error, 0, "the project file could not be replaced");
        }
        return true;
    }

    String ResolveScriptModulePath(const ProjectFile& project, const char* projectFilePath)
    {
        return ResolveProjectRelativePath(project.scriptOutputLibraryPath.c_str(), projectFilePath);
    }

    String ResolveProjectRelativePath(const char* relativePath, const char* projectFilePath)
    {
        const String relative(relativePath != nullptr ? relativePath : "");
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
