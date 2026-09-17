// JBroScript 컴파일러의 명령줄이다(D-105).
//
//     JBroc [--locale ko-KR|en-US] [--dump-tree] <파일.jscript>...
//
// 진단은 표준 출력에 한 줄씩 MSVC 모양으로 낸다. VS Code 의 $msCompile 매처가 그대로 읽고, 그 매처는 경로를 절대 경로로만
// 받으므로 경로는 절대 경로로 바꿔 낸다. 명령줄과 파일의 문제는 표준 에러로 낸다.
//
// 종료 코드: 0 에러 없음, 1 소스에 에러가 있음, 2 명령줄이나 파일에 문제가 있음(둘 다 있으면 2), 3 JBroc 자체의 결함.
//
// 인자는 wmain 으로 받는다. 한국어 Windows 에서 main 의 char 인자는 코드 페이지 949 라 한글 경로가 깨진다.

#include <JBro/ScriptCompiler/DiagnosticMessages.h>
#include <JBro/ScriptCompiler/Parser.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <crtdbg.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include <Windows.h>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    constexpr int ExitSuccess = 0;
    constexpr int ExitSourceErrors = 1;
    constexpr int ExitUsageOrInput = 2;
    // JBroc 의 결함이다. 사용자의 입력으로는 나오면 안 된다.
    constexpr int ExitInternalError = 3;

    String ToUtf8(std::wstring_view text)
    {
        String result;
        if (text.empty())
        {
            return result;
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        result.Std().resize(static_cast<std::size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToWide(std::string_view text)
    {
        std::wstring result;
        if (text.empty())
        {
            return result;
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        result.resize(static_cast<std::size_t>(size));
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    // 콘솔이면 글자로 쓰고(콘솔 코드 페이지와 무관하게 한글이 보인다), 파이프나 파일이면 UTF-8 바이트로 쓴다.
    // 편집기의 태스크는 파이프로 읽으므로 UTF-8 을 받는다.
    void Write(DWORD stream, std::string_view utf8)
    {
        const HANDLE handle = GetStdHandle(stream);
        if (INVALID_HANDLE_VALUE == handle || nullptr == handle || utf8.empty())
        {
            return;
        }
        DWORD written = 0;
        DWORD mode = 0;
        if (0 != GetConsoleMode(handle, &mode))
        {
            const std::wstring wide = ToWide(utf8);
            WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
            return;
        }
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    void WriteLine(DWORD stream, std::string_view utf8)
    {
        Write(stream, utf8);
        Write(stream, "\n");
    }

    std::filesystem::path GetExecutableDirectory()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (0 == length)
            {
                return std::filesystem::path();
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        return std::filesystem::path(buffer).parent_path();
    }

    bool ReadWholeFile(const std::filesystem::path& path, String& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (false == file.is_open())
        {
            return false;
        }
        out = String(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        return false == file.bad();
    }

    String LocalizedText(const DiagnosticMessages& messages, const char* key, std::string_view argument)
    {
        const String value(argument);
        return messages.FormatKey(key, ArrayView<const String>(&value, 1));
    }

    String LocalizedText(const DiagnosticMessages& messages, const char* key)
    {
        return messages.FormatKey(key, ArrayView<const String>());
    }

    struct CommandLine
    {
        String Locale = "ko-KR";
        bool DumpTree = false;
        bool Help = false;
        Array<std::filesystem::path> Files;
        // 명령줄이 틀렸으면 그 알림의 키와 인자다. 번역 표를 읽은 뒤에 낸다(언어가 인자에 있으므로).
        const char* ProblemKey = nullptr;
        String ProblemArgument;
    };

    CommandLine ReadCommandLine(int argc, wchar_t** argv)
    {
        CommandLine commandLine;
        for (int index = 1; index < argc; ++index)
        {
            const std::wstring_view argument(argv[index]);
            if (L"--help" == argument || L"-h" == argument)
            {
                commandLine.Help = true;
                continue;
            }
            if (L"--dump-tree" == argument)
            {
                commandLine.DumpTree = true;
                continue;
            }
            if (L"--locale" == argument)
            {
                if (index + 1 >= argc)
                {
                    commandLine.ProblemKey = "jbroc.cli.missing_option_value";
                    commandLine.ProblemArgument = "--locale";
                    continue;
                }
                commandLine.Locale = ToUtf8(argv[++index]);
                continue;
            }
            if (argument.size() > 1 && L'-' == argument[0])
            {
                if (nullptr == commandLine.ProblemKey)
                {
                    commandLine.ProblemKey = "jbroc.cli.unknown_option";
                    commandLine.ProblemArgument = ToUtf8(argument);
                }
                continue;
            }
            commandLine.Files.Add(std::filesystem::path(argument));
        }
        return commandLine;
    }
}

int Run(int argc, wchar_t** argv);

int wmain(int argc, wchar_t** argv)
{
    // **단언이 대화상자를 띄우면 안 된다(D-69 와 같은 이유).** JBroc 은 편집기의 빌드 태스크가 사람 없이 부른다.
    // 대화상자는 누를 사람이 없어 영원히 멈추고, 멈춘 것은 실패한 것보다 나쁘다. 표준 에러로 알리고 끝낸다.
    _set_error_mode(_OUT_TO_STDERR);
    for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // 잡히지 않은 예외는 아무 말 없이 종료 코드 3 으로 죽는다. 무엇이 죽었는지 표준 에러에 남긴다.
    try
    {
        return Run(argc, argv);
    }
    catch (const std::exception& exception)
    {
        WriteLine(STD_ERROR_HANDLE, String("jbroc: internal error: ").Append(exception.what()).View());
    }
    catch (...)
    {
        WriteLine(STD_ERROR_HANDLE, "jbroc: internal error");
    }
    return ExitInternalError;
}

int Run(int argc, wchar_t** argv)
{
    CommandLine commandLine = ReadCommandLine(argc, argv);

    const std::filesystem::path localizationDirectory = GetExecutableDirectory() / L"Localization" / L"jbroc";
    const String directory = ToUtf8(localizationDirectory.native());
    DiagnosticMessages messages;
    if (false == messages.Load(directory.c_str(), commandLine.Locale.c_str(), "en-US"))
    {
        if (messages.Load(directory.c_str(), "en-US", nullptr))
        {
            // 표는 있는데 그 언어가 없다. 영어로 알리고 멈춘다.
            if (nullptr == commandLine.ProblemKey)
            {
                commandLine.ProblemKey = "jbroc.cli.unknown_locale";
                commandLine.ProblemArgument = commandLine.Locale;
            }
        }
        else
        {
            // 번역 표가 아예 없다. 메시지는 키로 나가지만 에러를 가르는 일은 그대로 한다.
            WriteLine(STD_ERROR_HANDLE, String("jbroc: warning: no diagnostic messages in ").Append(directory.View()).Append(
                "; messages are shown as keys").View());
        }
    }

    if (nullptr != commandLine.ProblemKey)
    {
        WriteLine(STD_ERROR_HANDLE, LocalizedText(messages, commandLine.ProblemKey, commandLine.ProblemArgument.View()).View());
        WriteLine(STD_ERROR_HANDLE, LocalizedText(messages, "jbroc.cli.usage").View());
        return ExitUsageOrInput;
    }
    if (commandLine.Help)
    {
        WriteLine(STD_OUTPUT_HANDLE, LocalizedText(messages, "jbroc.cli.usage").View());
        return ExitSuccess;
    }
    if (commandLine.Files.IsEmpty())
    {
        WriteLine(STD_ERROR_HANDLE, LocalizedText(messages, "jbroc.cli.no_input").View());
        WriteLine(STD_ERROR_HANDLE, LocalizedText(messages, "jbroc.cli.usage").View());
        return ExitUsageOrInput;
    }

    int exitCode = ExitSuccess;
    for (const std::filesystem::path& file : commandLine.Files)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(file, error);
        if (error)
        {
            absolute = file;
        }
        const String path = ToUtf8(absolute.lexically_normal().native());

        String text;
        if (false == ReadWholeFile(absolute, text))
        {
            WriteLine(STD_ERROR_HANDLE, LocalizedText(messages, "jbroc.cli.cannot_read_file", path.View()).View());
            exitCode = ExitUsageOrInput;
            continue;
        }

        const SourceText source(path, std::move(text));
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        for (const Diagnostic& diagnostic : diagnostics.GetItems())
        {
            WriteLine(STD_OUTPUT_HANDLE, FormatDiagnosticLine(path.View(), diagnostic, messages).View());
        }
        if (diagnostics.HasErrors() && ExitSuccess == exitCode)
        {
            exitCode = ExitSourceErrors;
        }
        if (commandLine.DumpTree)
        {
            Write(STD_OUTPUT_HANDLE, DumpSyntaxTree(tree).View());
        }
    }
    return exitCode;
}
