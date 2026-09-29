#include <JBro/Editor/ScriptBuild.h>

#include <JBro/Editor/EditorPaths.h>
#include <JBro/Platform/Platform.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace JBro::ScriptBuild
{
    namespace
    {
        const char* const EditorKindNames[] = {"System", "VisualStudio", "VisualStudioCode"};

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        String Trimmed(const char* begin, const char* end)
        {
            while (begin < end && IsSpace(*begin))
            {
                ++begin;
            }
            while (end > begin && IsSpace(end[-1]))
            {
                --end;
            }
            return String(begin, static_cast<std::size_t>(end - begin));
        }

        // `vswhere` 가 있는 자리다. VS 설치와 무관하게 여기에 깔린다(기존 엔진과 같은 판단).
        String VswherePath(IPlatform& platform)
        {
            const String programFiles = platform.ReadEnvironmentVariable("ProgramFiles(x86)");
            if (programFiles.empty())
            {
                return String();
            }
            const String path = EditorPaths::JoinPath(programFiles.c_str(), "Microsoft Visual Studio/Installer/vswhere.exe");
            return platform.FileExists(path.c_str()) ? path : String();
        }

        // `vswhere` 를 띄우고 끝을 기다려 첫 줄 중 있는 파일을 돌려준다. 짧게 끝나는 도구라 기다린다(10 초 상한).
        String RunVswhere(IPlatform& platform, const char* scratchFolder, const char* arguments)
        {
            const String vswhere = VswherePath(platform);
            if (vswhere.empty() || scratchFolder == nullptr || scratchFolder[0] == '\0')
            {
                return String();
            }
            platform.CreateDirectoryAt(scratchFolder);
            const String output = EditorPaths::JoinPath(scratchFolder, "vswhere.txt");
            String command = "\"";
            command += vswhere;
            command += "\" ";
            command += arguments;
            ChildProcess process = platform.StartProcess(command.c_str(), nullptr, output.c_str());
            std::int32_t exitCode = 0;
            ProcessStatus status = platform.PollProcess(process, exitCode);
            for (int waited = 0; status == ProcessStatus::Running && waited < 1000; ++waited)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                status = platform.PollProcess(process, exitCode);
            }
            platform.CloseProcess(process);
            Array<std::byte> bytes;
            if (status != ProcessStatus::Exited || false == platform.ReadWholeFile(output.c_str(), bytes))
            {
                return String();
            }
            const char* text = reinterpret_cast<const char*>(bytes.Data());
            const char* end = text + bytes.Size();
            while (text < end)
            {
                const char* lineEnd = text;
                while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
                {
                    ++lineEnd;
                }
                const String candidate = Trimmed(text, lineEnd);
                if (false == candidate.empty() && platform.FileExists(candidate.c_str()))
                {
                    return candidate;
                }
                text = lineEnd + 1;
            }
            return String();
        }

        bool ParseNumber(const char*& cursor, const char* end, std::uint32_t& value)
        {
            const char* start = cursor;
            std::uint32_t result = 0;
            while (cursor < end && *cursor >= '0' && *cursor <= '9')
            {
                result = result * 10 + static_cast<std::uint32_t>(*cursor - '0');
                ++cursor;
            }
            value = result;
            return cursor > start;
        }
    }

    const char* EditorKindName(EditorKind kind)
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return index < static_cast<std::size_t>(EditorKind::Count) ? EditorKindNames[index] : EditorKindNames[0];
    }

    bool ParseEditorKind(const char* text, EditorKind& out)
    {
        for (std::size_t index = 0; index < static_cast<std::size_t>(EditorKind::Count); ++index)
        {
            if (text != nullptr && std::strcmp(text, EditorKindNames[index]) == 0)
            {
                out = static_cast<EditorKind>(index);
                return true;
            }
        }
        return false;
    }

    bool ParseDiagnosticLine(const char* line, std::size_t length, Diagnostic& out)
    {
        // MSBuild 의 표준 진단 모양이다: `근원: 분류 코드: 글`. 분류(`error`·`fatal error`·`warning`)는 한국어 VS 에서도 영어로 적힌다.
        // `fatal error` 는 헤더를 못 찾은 컴파일러(`C1083`)와 라이브러리를 못 연 링커(`LNK1104`)가 쓴다 - 빠뜨리면 실패한 빌드에 진단이 없다.
        const char* end = line + length;
        const char* keyword = nullptr;
        bool isError = true;
        std::size_t keywordLength = 0;
        for (const char* cursor = line; cursor + 2 < end; ++cursor)
        {
            if (cursor[0] != ':' || cursor[1] != ' ')
            {
                continue;
            }
            const std::size_t left = static_cast<std::size_t>(end - (cursor + 2));
            if (left >= 12 && std::strncmp(cursor + 2, "fatal error ", 12) == 0)
            {
                keyword = cursor;
                keywordLength = 12;
                isError = true;
                break;
            }
            if (left >= 6 && std::strncmp(cursor + 2, "error ", 6) == 0)
            {
                keyword = cursor;
                keywordLength = 6;
                isError = true;
                break;
            }
            if (left >= 8 && std::strncmp(cursor + 2, "warning ", 8) == 0)
            {
                keyword = cursor;
                keywordLength = 8;
                isError = false;
                break;
            }
        }
        if (keyword == nullptr)
        {
            return false;
        }

        Diagnostic diagnostic;
        diagnostic.isError = isError;

        // 근원: `경로(줄,칸)`·`경로(줄)` 이면 파일이다. `LINK`·`MSBUILD` 처럼 괄호가 없으면 파일이 없는 진단이다.
        String origin = Trimmed(line, keyword);
        if (false == origin.empty() && origin.back() == ')')
        {
            const std::size_t open = origin.rfind('(');
            if (open != String::npos && open > 0)
            {
                const char* cursor = origin.c_str() + open + 1;
                const char* close = origin.c_str() + origin.size() - 1;
                std::uint32_t number = 0;
                if (ParseNumber(cursor, close, number))
                {
                    diagnostic.line = number;
                    if (cursor < close && *cursor == ',')
                    {
                        ++cursor;
                        ParseNumber(cursor, close, diagnostic.column);
                    }
                    diagnostic.file = origin.substr(0, open);
                }
            }
        }

        // 코드와 글: `C2065: 'spd': 선언되지 않은 식별자입니다. [F:\...\GameScript.vcxproj]`
        const char* rest = keyword + 2 + keywordLength;
        const char* colon = rest;
        while (colon < end && *colon != ':' && false == IsSpace(*colon))
        {
            ++colon;
        }
        const char* messageStart = rest;
        if (colon < end && *colon == ':')
        {
            diagnostic.code = String(rest, static_cast<std::size_t>(colon - rest));
            messageStart = colon + 1;
        }
        const char* messageEnd = end;
        while (messageEnd > messageStart && IsSpace(messageEnd[-1]))
        {
            --messageEnd;
        }
        // 끝의 `[프로젝트 경로]` 는 모든 줄에 같게 붙는다. 뗀다.
        if (messageEnd > messageStart && messageEnd[-1] == ']')
        {
            const char* bracket = messageEnd - 1;
            while (bracket > messageStart && *bracket != '[')
            {
                --bracket;
            }
            if (*bracket == '[')
            {
                messageEnd = bracket;
            }
        }
        diagnostic.message = Trimmed(messageStart, messageEnd);
        out = std::move(diagnostic);
        return true;
    }

    void ParseLog(const char* text, std::size_t length, Array<Diagnostic>& out)
    {
        out.Clear();
        const char* cursor = text;
        const char* end = text + length;
        // 파일 로거가 UTF-8 BOM 을 앞에 둔다.
        if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF)
        {
            cursor += 3;
        }
        while (cursor < end)
        {
            const char* lineEnd = cursor;
            while (lineEnd < end && *lineEnd != '\n')
            {
                ++lineEnd;
            }
            Diagnostic diagnostic;
            if (ParseDiagnosticLine(cursor, static_cast<std::size_t>(lineEnd - cursor), diagnostic))
            {
                bool seen = false;
                for (const Diagnostic& earlier : out)
                {
                    seen = seen || (earlier.file == diagnostic.file && earlier.line == diagnostic.line
                        && earlier.column == diagnostic.column && earlier.code == diagnostic.code
                        && earlier.message == diagnostic.message);
                }
                if (false == seen)
                {
                    out.Add(std::move(diagnostic));
                }
            }
            cursor = lineEnd + 1;
        }
    }

    String LocateMSBuild(IPlatform& platform, const char* scratchFolder)
    {
        // 개발자 명령 프롬프트에서 띄웠으면 그 VS 다.
        const String installDir = platform.ReadEnvironmentVariable("VSINSTALLDIR");
        if (false == installDir.empty())
        {
            const String candidate = EditorPaths::JoinPath(installDir.c_str(), "MSBuild/Current/Bin/MSBuild.exe");
            if (platform.FileExists(candidate.c_str()))
            {
                return candidate;
            }
        }
        // `-prerelease` 로 Preview 판도, `-requires` 로 C++ 빌드 도구가 있는 것만 본다.
        return RunVswhere(platform, scratchFolder,
            "-latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe -utf8");
    }

    String LocateVisualStudio(IPlatform& platform, const char* scratchFolder)
    {
        return RunVswhere(platform, scratchFolder, "-latest -prerelease -property productPath -utf8");
    }

    String LocateVisualStudioCode(IPlatform& platform)
    {
        const char* const roots[] = {"LOCALAPPDATA", "ProgramFiles"};
        const char* const relatives[] = {"Programs/Microsoft VS Code/Code.exe", "Microsoft VS Code/Code.exe"};
        for (std::size_t index = 0; index < 2; ++index)
        {
            const String root = platform.ReadEnvironmentVariable(roots[index]);
            if (root.empty())
            {
                continue;
            }
            const String candidate = EditorPaths::JoinPath(root.c_str(), relatives[index]);
            if (platform.FileExists(candidate.c_str()))
            {
                return candidate;
            }
        }
        return String();
    }

    String MakeBuildCommand(const char* msbuild, const char* projectFile, const char* configuration, const char* logFile)
    {
        String command = "\"";
        command += msbuild;
        command += "\" \"";
        command += projectFile;
        command += "\" -nologo -m -nr:false -p:Configuration=";
        command += configuration;
        // 콘솔은 조용히 두고 진단은 파일 로거가 UTF-8 로 적는다. 콘솔 출력은 시스템 코드 페이지라 한글 경로가 깨진다.
        command += " -p:Platform=x64 -v:quiet -fl \"-flp:LogFile=";
        command += logFile;
        command += ";Encoding=UTF-8;Verbosity=minimal\"";
        return command;
    }

    String MakeOpenAtLineCommand(EditorKind kind, const char* editorPath, const char* file, std::uint32_t line)
    {
        if (kind == EditorKind::System || editorPath == nullptr || editorPath[0] == '\0' || file == nullptr)
        {
            return String();
        }
        char lineText[16] = {};
        std::snprintf(lineText, sizeof(lineText), "%u", line > 0 ? line : 1u);
        String command = "\"";
        command += editorPath;
        command += "\" ";
        if (kind == EditorKind::VisualStudio)
        {
            // 떠 있는 Visual Studio 가 있으면 그 창에서 연다(`/Edit`). 줄은 명령으로 옮긴다.
            command += "/Edit \"";
            command += file;
            command += "\" /Command \"Edit.GoTo ";
            command += lineText;
            command += "\"";
        }
        else
        {
            command += "-g \"";
            command += file;
            command += ":";
            command += lineText;
            command += "\"";
        }
        return command;
    }
}
