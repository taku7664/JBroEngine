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

        // 버스 이펙트 키 → 칸. 모르는 키면 null 이다. 쓰는 쪽도 이 차례로 적는다.
        struct AudioEffectKey
        {
            const char* key;
            float AudioBusEffects::* field;
        };
        const AudioEffectKey AudioEffectKeys[] = {
            {"LowPass", &AudioBusEffects::lowPassHz},
            {"HighPass", &AudioBusEffects::highPassHz},
            {"EchoDelay", &AudioBusEffects::echoDelay},
            {"EchoFeedback", &AudioBusEffects::echoFeedback},
            {"EchoMix", &AudioBusEffects::echoMix},
            {"ReverbRoom", &AudioBusEffects::reverbRoom},
            {"ReverbDamping", &AudioBusEffects::reverbDamping},
            {"ReverbMix", &AudioBusEffects::reverbMix}};

        float* AudioBusEffectField(AudioBusEffects& effects, const String& key)
        {
            for (const AudioEffectKey& entry : AudioEffectKeys)
            {
                if (key == entry.key)
                {
                    return &(effects.*entry.field);
                }
            }
            return nullptr;
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
        // `AudioBuses:` 아래에 있는가. 맵의 시퀀스라 스칼라 시퀀스(`currentSequence`)와 따로 읽는다.
        bool           inAudioBuses = false;

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
                inAudioBuses = false;
            }

            // 오디오 버스(D-197): `- Name: X` 가 항목을 열고 그 아래 `Volume: v` 가 붙는다.
            if (inAudioBuses)
            {
                const bool opens = content[0] == '-' && (content[1] == ' ' || content[1] == '\0');
                const char* entry = opens ? content + 1 : content;
                while (entry < contentEnd && *entry == ' ')
                {
                    ++entry;
                }
                if (opens)
                {
                    parsed.audioBuses.Emplace();
                    if (entry >= contentEnd)
                    {
                        if (atEnd)
                        {
                            break;
                        }
                        continue;
                    }
                }
                else if (parsed.audioBuses.IsEmpty())
                {
                    return Fail(error, lineNumber, "an audio bus field has no `- Name:` above it");
                }
                const char* busColon = std::strchr(entry, ':');
                if (busColon == nullptr)
                {
                    return Fail(error, lineNumber, "an audio bus is `Name:` and `Volume:`");
                }
                const String busKey = Trim(entry, busColon);
                String busValue = Trim(busColon + 1, contentEnd);
                if (false == Unquote(busValue))
                {
                    return Fail(error, lineNumber, "unterminated quoted string");
                }
                ProjectAudioBus& bus = parsed.audioBuses.Last();
                if (busKey == "Name")
                {
                    bus.name = busValue;
                }
                else if (busKey == "Volume")
                {
                    if (false == ParseFloat(busValue, bus.volume))
                    {
                        return Fail(error, lineNumber, "an audio bus Volume must be a number");
                    }
                }
                else if (float* effect = AudioBusEffectField(bus.effects, busKey))
                {
                    if (false == ParseFloat(busValue, *effect))
                    {
                        return Fail(error, lineNumber, "an audio bus effect must be a number");
                    }
                }
                // 모르는 필드는 두고 지나간다(뒤의 판이 더할 수 있다).
                if (atEnd)
                {
                    break;
                }
                continue;
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
                else if (indent == 0 && key == "AudioBuses")
                {
                    parsed.audioBuses.Clear();
                    inAudioBuses = true;
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
            else if (key == "AudioBuses")
            {
                // 같은 줄에 값이 있는 것은 빈 목록(`[]`)뿐이다.
                parsed.audioBuses.Clear();
                recognized = value == "[]";
            }
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

    namespace
    {
        std::size_t IndentOf(const String& line)
        {
            std::size_t spaces = 0;
            while (spaces < line.size() && line[spaces] == ' ')
            {
                ++spaces;
            }
            return spaces;
        }

        // `AssetIgnorePatterns` 를 적는다(D-189). 비어 있으면 `[]` 다 - 머리줄만 두면
        // 다음에 읽을 때 그 키가 값 없는 맵으로 보인다.
        //
        // 항목은 늘 따옴표로 감싼다. `*.tmp` 는 따옴표 없이도 읽히지만 `~$*` 처럼
        // YAML 이 다르게 읽는 글자로 시작하는 것이 있다.
        // 이 키를 이미 적었는가. 적었으면 참을 돌려주고, 아니면 적었다고 표시한다.
        // 아는 키 목록에 없으면 거짓이다 - 부르는 쪽이 이미 걸렀지만 여기서도 안전하다.
        template <std::size_t Count>
        bool MarkWritten(Array<bool>& wrote, const char* const (&keys)[Count], const String& key)
        {
            for (std::size_t index = 0; index < Count && index < wrote.Size(); ++index)
            {
                if (key == keys[index])
                {
                    const bool already = wrote[index];
                    wrote[index] = true;
                    return already;
                }
            }
            return false;
        }

        // 값을 지키는 가장 짧은 글자다. `%.9g` 로 적으면 사람이 적은 `0.8` 이 `0.800000012` 가 되어, 고친 것이 없는
        // 저장이 파일을 바꾼다(D-189). 6 자리부터 늘려 가며 도로 읽어 같은 값이 되는 첫 것을 쓴다.
        String FormatShortFloat(float value)
        {
            char buffer[32] = {};
            for (int digits = 6; digits <= 9; ++digits)
            {
                std::snprintf(buffer, sizeof(buffer), "%.*g", digits, static_cast<double>(value));
                if (static_cast<float>(std::strtod(buffer, nullptr)) == value)
                {
                    break;
                }
            }
            return String(buffer);
        }

        // 따옴표 없이 적어도 되는 이름인가. 기존 엔진의 파일은 따옴표 없이 적혀 있다.
        bool IsPlainName(const String& name)
        {
            if (name.empty() || name[0] == ' ' || name[name.size() - 1] == ' ')
            {
                return false;
            }
            for (const char character : name)
            {
                if (std::strchr(":#'\"[]{},&*!|>%@`", character) != nullptr)
                {
                    return false;
                }
            }
            return name[0] != '-' && name[0] != '?';
        }

        // `AudioBuses` 를 적는다(D-197). 비어 있으면 `[]` 다.
        void AppendAudioBuses(String& result, const ProjectFile& project)
        {
            if (project.audioBuses.IsEmpty())
            {
                result.append("AudioBuses: []\n", 15);
                return;
            }
            result.append("AudioBuses:\n", 12);
            for (std::size_t index = 0; index < project.audioBuses.Size(); ++index)
            {
                const ProjectAudioBus& bus = project.audioBuses[index];
                result.append("  - Name: ", 10);
                if (IsPlainName(bus.name))
                {
                    result.append(bus.name.c_str(), bus.name.size());
                }
                else
                {
                    result.append("\"", 1);
                    result.append(bus.name.c_str(), bus.name.size());
                    result.append("\"", 1);
                }
                result.append("\n    Volume: ", 13);
                const String volume = FormatShortFloat(bus.volume);
                result.append(volume.c_str(), volume.size());
                result.append("\n", 1);
                // 이펙트는 기본값과 다른 칸만 적는다. 이펙트를 쓰지 않는 파일은 전과 같다.
                const AudioBusEffects defaults;
                for (const AudioEffectKey& entry : AudioEffectKeys)
                {
                    const float value = bus.effects.*entry.field;
                    if (value == defaults.*entry.field)
                    {
                        continue;
                    }
                    result.append("    ", 4);
                    result.append(entry.key, std::strlen(entry.key));
                    result.append(": ", 2);
                    const String text = FormatShortFloat(value);
                    result.append(text.c_str(), text.size());
                    result.append("\n", 1);
                }
            }
        }

        void AppendIgnorePatterns(String& result, const ProjectFile& project)
        {
            if (project.assetIgnorePatterns.IsEmpty())
            {
                result.append("AssetIgnorePatterns: []\n", 24);
                return;
            }
            result.append("AssetIgnorePatterns:\n", 21);
            for (std::size_t index = 0; index < project.assetIgnorePatterns.Size(); ++index)
            {
                const String& pattern = project.assetIgnorePatterns[index];
                result.append("  - \"", 5);
                result.append(pattern.c_str(), pattern.size());
                result.append("\"\n", 2);
            }
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
        // **지금 고쳐 쓰는 시퀀스가 있는가**(D-189). 시퀀스는 값이 여러 줄이라 한 줄
        // 바꿔치기로는 다룰 수 없다 - 머리줄을 새로 적고 원문의 항목 줄들은 건너뛴다.
        bool skippingSequence = false;
        bool sawIgnorePatterns = false;
        bool sawAudioBuses = false;
        // `Build:` 블록이 끝나는 자리. 없던 키를 그 끝에 더한다.
        std::size_t buildEnd = String::npos;

        std::size_t at = 0;
        // 빈 원문(새 프로젝트, D-160)은 줄이 하나도 없다. 빈 줄 하나로 세면 파일이 빈 줄로 시작한다.
        // **마지막 줄바꿈 뒤는 줄이 아니다**(D-189). `at <= originalLength` 로 세면 파일 끝의
        // `\n` 다음 자리가 빈 줄 하나로 잡혀, 저장할 때마다 파일 끝에 빈 줄이 하나씩 쌓였다
        // (실제 프로젝트 파일이 그렇게 벌어져 있었다). 줄바꿈으로 끝나지 않는 원문의
        // 마지막 줄은 `at < originalLength` 로도 그대로 들어온다.
        while (at < originalLength)
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

            // 시퀀스를 고쳐 쓰는 중이면 원문의 항목 줄은 버린다. 다음 최상위 키에서 멈춘다.
            if (skippingSequence)
            {
                if (false == IsBlankOrComment(line) && IndentOf(line) > 0)
                {
                    if (stop >= originalLength)
                    {
                        break;
                    }
                    at = stop + 1;
                    continue;
                }
                skippingSequence = false;
            }

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
            bool dropped = false;
            if (pair && indent == 0 && key == "AudioBuses")
            {
                // 시퀀스라 머리줄에서 새로 적고 원문의 항목 줄들을 건너뛴다(`AssetIgnorePatterns` 와 같다).
                dropped = sawAudioBuses;
                if (false == dropped)
                {
                    AppendAudioBuses(result, project);
                    sawAudioBuses = true;
                }
                skippingSequence = false == hasValue;
                replaced = true;
            }
            else if (pair && indent == 0 && key == "AssetIgnorePatterns")
            {
                // 두 번째부터는 지운다. 시퀀스를 한 번 적었으면 그것이 전부다.
                dropped = sawIgnorePatterns;
                if (false == dropped)
                {
                    AppendIgnorePatterns(result, project);
                    sawIgnorePatterns = true;
                }
                // 값이 같은 줄에 있으면(`[]`) 뒤따르는 항목 줄이 없다.
                skippingSequence = false == hasValue;
                replaced = true;
            }
            // **값이 비어 있어도 아는 키다.** `hasValue` 로 거르면 `ProductName: ` 같은 줄이
            // "적지 않은 키" 로 남아, 저장할 때마다 같은 키가 파일 뒤에 하나씩 더 붙었다
            // (실제 프로젝트 파일이 그렇게 불어나 있었다). 모르는 키는 아래 두 함수가
            // 거짓을 돌려주므로 블록 머리줄(`Build:`)은 그대로 지나간다.
            else if (pair)
            {
                if (indent == 0 && TopLevelValue(project, key, value))
                {
                    // **이미 적은 키면 이 줄은 지운다**(D-189). 예전 결함으로 같은 키가
                    // 여러 줄 적힌 파일이 실제로 있고, 읽을 때는 마지막 줄이 앞의 줄을
                    // 조용히 덮는다 - 값이 다르면 무엇이 맞는지 파일만 보고 알 수 없다.
                    // 매핑에 같은 키가 두 번 있을 수 없으니 지우는 것이 고치는 것이다.
                    dropped = MarkWritten(wroteTopLevel, TopLevelKeys, key);
                    if (false == dropped)
                    {
                        AppendPair(result, "", key, value);
                    }
                    replaced = true;
                }
                else if (indent == 2 && inBuild && BuildValue(project, key, value))
                {
                    dropped = MarkWritten(wroteBuild, BuildKeys, key);
                    if (false == dropped)
                    {
                        AppendPair(result, "  ", key, value);
                    }
                    replaced = true;
                }
            }
            // `dropped` 는 여기서 보지 않는다. 지운 줄은 위의 세 갈래가 모두
            // `replaced` 로 표시해 두므로, 두 조건을 함께 걸면 한쪽이 잴 수 없는 줄이 된다.
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

        // 적힌 적 없는 무시 패턴은 맨 뒤에 붙인다. 비어 있으면 적지 않는다 -
        // 빈 시퀀스를 새로 만들어 넣으면 손대지 않은 파일이 저장만으로 길어진다.
        if (false == sawIgnorePatterns && false == project.assetIgnorePatterns.IsEmpty())
        {
            AppendIgnorePatterns(result, project);
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

        // 적힌 적 없는 버스는 **맨 뒤에** 붙인다. 새 파일은 첫 줄이 `Version` 이어야 한다 - 앞에 두면 새 프로젝트 파일이
        // 버스 목록으로 시작한다. 비어 있으면 적지 않는다.
        if (false == sawAudioBuses && false == project.audioBuses.IsEmpty())
        {
            AppendAudioBuses(result, project);
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

    bool CreateProjectFile(IPlatform& platform, const char* parentFolder, const char* name,
        FrameworkKind framework, const char* engineVersion, String& outProjectFilePath,
        ProjectFileError& error)
    {
        error = ProjectFileError{};
        outProjectFilePath.clear();
        const auto fail = [&error](ProjectCreateFailure failure, const char* message) {
            Fail(error, 0, message);
            error.createFailure = failure;
            return false;
        };
        if (parentFolder == nullptr || parentFolder[0] == '\0')
        {
            return fail(ProjectCreateFailure::CannotWrite, "no folder was given");
        }
        if (engineVersion == nullptr || engineVersion[0] == '\0')
        {
            return fail(ProjectCreateFailure::NoEngineVersion, "the project must say which engine version it opens with (EngineVersion)");
        }
        // **이름은 폴더와 파일의 이름이 된다.** 경로를 가르는 글자나 윈도우가 받지 않는 글자가 들어가면
        // 엉뚱한 곳에 서거나 만들다 만다. 앞뒤 공백과 `.`·`..` 도 같은 이유로 막는다.
        const String projectName(name != nullptr ? name : "");
        if (projectName.empty() || projectName == "." || projectName == ".."
            || projectName[0] == ' ' || projectName[projectName.size() - 1] == ' '
            || projectName[projectName.size() - 1] == '.')
        {
            return fail(ProjectCreateFailure::InvalidName, "the name cannot be a file name");
        }
        for (const char character : projectName)
        {
            if (static_cast<unsigned char>(character) < 0x20 || std::strchr("\\/:*?\"<>|", character) != nullptr)
            {
                return fail(ProjectCreateFailure::InvalidName, "the name cannot be a file name");
            }
        }

        String root(parentFolder);
        if (root[root.size() - 1] != '/' && root[root.size() - 1] != '\\')
        {
            root.append("/", 1);
        }
        root.append(projectName.c_str(), projectName.size());
        if (platform.DirectoryExists(root.c_str()) || platform.FileExists(root.c_str()))
        {
            return fail(ProjectCreateFailure::AlreadyExists, "something with that name is already in the folder");
        }

        ProjectFile project;
        project.engineVersion = engineVersion;
        project.framework = framework;
        project.build.productName = projectName;
        // 새 프로젝트는 흔한 둘로 시작한다. 옵션 화면의 "배경음·효과음" 이 곧바로 버스 이름에 닿는다.
        project.audioBuses.Add(ProjectAudioBus{String("Music"), 1.0f});
        project.audioBuses.Add(ProjectAudioBus{String("SFX"), 1.0f});
        String assets = root;
        assets.append("/", 1);
        assets.append(project.assetDirectory.c_str(), project.assetDirectory.size());
        if (false == platform.CreateDirectoryAt(assets.c_str()))
        {
            return fail(ProjectCreateFailure::CannotWrite, "the project folder could not be created");
        }

        // 빈 원문에 쓰면 아는 키가 모두 제 차례로 붙는다 - 새 파일의 모양도 고쳐 쓰는 길과 같다.
        String text;
        if (false == WriteProjectFileText(project, "", 0, text, error))
        {
            error.createFailure = ProjectCreateFailure::CannotWrite;
            return false;
        }
        String path = root;
        path.append("/", 1);
        path.append(projectName.c_str(), projectName.size());
        path.append(".jproject", 9);
        const JArrayView<std::byte> bytes{
            reinterpret_cast<const std::byte*>(text.c_str()),
            static_cast<std::uint32_t>(text.size())};
        if (false == platform.WriteWholeFile(path.c_str(), bytes))
        {
            return fail(ProjectCreateFailure::CannotWrite, "the project file could not be written");
        }
        // 쓴 것이 열리는지 본다. 열리지 않는 프로젝트를 만들어 놓고 성공이라 하지 않는다.
        ProjectFile check;
        if (false == LoadProjectFile(platform, path.c_str(), check, error))
        {
            error.createFailure = ProjectCreateFailure::CannotWrite;
            return false;
        }
        outProjectFilePath = path;
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

    String MakeProjectRelativePath(const char* absolutePath, const char* projectFilePath)
    {
        const String path(absolutePath != nullptr ? absolutePath : "");
        if (path.empty() || projectFilePath == nullptr)
        {
            return path;
        }
        String root(projectFilePath);
        const std::size_t slash = root.find_last_of("/\\");
        if (slash == String::npos)
        {
            return path;
        }
        root.resize(slash);
        return MakeFolderRelativePath(path.c_str(), root.c_str());
    }

    String MakeFolderRelativePath(const char* absolutePath, const char* folder)
    {
        const String path(absolutePath != nullptr ? absolutePath : "");
        String root(folder != nullptr ? folder : "");
        while (false == root.empty() && (root.back() == '/' || root.back() == '\\'))
        {
            root.pop_back();
        }
        if (path.empty() || root.empty())
        {
            return path;
        }
        const auto same = [](char left, char right) {
            const auto fold = [](char value) {
                if (value == '\\')
                {
                    return '/';
                }
                return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
            };
            return fold(left) == fold(right);
        };
        if (path.size() < root.size())
        {
            return path;
        }
        for (std::size_t index = 0; index < root.size(); ++index)
        {
            if (false == same(path[index], root[index]))
            {
                return path;
            }
        }
        if (path.size() == root.size())
        {
            return String(".");
        }
        // `C:/Game` 과 `C:/GameAssets` 는 다른 폴더다. 뿌리 뒤가 가르개여야 그 안이다.
        if (path[root.size()] != '/' && path[root.size()] != '\\')
        {
            return path;
        }
        String relative(path.c_str() + root.size() + 1);
        for (char& character : relative)
        {
            if (character == '\\')
            {
                character = '/';
            }
        }
        return relative.empty() ? String(".") : relative;
    }
}
