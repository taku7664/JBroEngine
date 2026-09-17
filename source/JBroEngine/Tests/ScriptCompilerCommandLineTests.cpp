// JBroc 실행 파일을 실제로 띄워 본다(D-105).
//
// 종료 코드, 표준 출력, 표준 에러를 따로 받는다. 출력 한 줄은 VS Code 의 $msCompile 정규식(upstream
// src/vs/workbench/contrib/tasks/common/problemMatcher.ts 의 원문)에 걸려야 한다 - 편집기가 에러를 문제 패널로 받는 길이다.
// JBroc.exe 는 이 테스트 실행 파일 옆에 빌드된다(JBroTests 가 프로젝트로 참조한다).

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/DiagnosticMessages.h>
#include <JBro/ScriptCompiler/Parser.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>

#include <Windows.h>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // $msCompile 의 정규식 원문이다. file 1, location 2, severity 4, code 5, message 6.
    const char* const MsCompilePattern =
        R"(^\s*(?:\s*\d+>)?(\S.*?)(?:\((\d+|\d+,\d+|\d+,\d+,\d+,\d+)\))?\s*:\s+(?:(\S+)\s+)?((?:fatal +)?error|warning|info)\s+(\w+\d+)?\s*:\s*(.*)$)";

    // 명령줄 알림의 키다. 번역 표에 진단 키와 이것만 있어야 한다.
    const char* const CommandLineKeys[] = {
        "jbroc.cli.usage",
        "jbroc.cli.no_input",
        "jbroc.cli.unknown_option",
        "jbroc.cli.missing_option_value",
        "jbroc.cli.unknown_locale",
        "jbroc.cli.cannot_read_file",
    };

    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return std::string();
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::filesystem::path TestExecutableDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        Check(0 != length && length < MAX_PATH, "the test executable path fits the buffer");
        return std::filesystem::path(buffer).parent_path();
    }

    std::string ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    void WriteBytes(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    struct RunResult
    {
        DWORD ExitCode = 0xFFFFFFFF;
        std::string Out;
        std::string Err;
    };

    // 표준 출력과 표준 에러를 파일로 돌려 받는다. 파일은 콘솔이 아니므로 JBroc 은 UTF-8 바이트로 쓴다.
    RunResult Run(const std::filesystem::path& executable, const std::wstring& arguments, const std::filesystem::path& scratch)
    {
        static int counter = 0;
        ++counter;
        const std::filesystem::path outPath = scratch / (L"out" + std::to_wstring(counter) + L".txt");
        const std::filesystem::path errPath = scratch / (L"err" + std::to_wstring(counter) + L".txt");

        SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE err = CreateFileW(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(INVALID_HANDLE_VALUE != out && INVALID_HANDLE_VALUE != err, "the output capture files open");

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nullptr;
        startup.hStdOutput = out;
        startup.hStdError = err;
        PROCESS_INFORMATION process{};
        std::wstring commandLine = L"\"" + executable.native() + L"\" " + arguments;
        const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        CloseHandle(out);
        CloseHandle(err);
        Check(0 != started, "JBroc starts");

        RunResult result;
        const DWORD waited = WaitForSingleObject(process.hProcess, 60000);
        if (WAIT_OBJECT_0 != waited)
        {
            TerminateProcess(process.hProcess, 99);
        }
        GetExitCodeProcess(process.hProcess, &result.ExitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Check(WAIT_OBJECT_0 == waited, "JBroc finishes within a minute");

        result.Out = ReadBytes(outPath);
        result.Err = ReadBytes(errPath);
        return result;
    }

    std::wstring Quote(const std::filesystem::path& path)
    {
        return L"\"" + path.native() + L"\"";
    }

    void Print(const RunResult& result)
    {
        std::cout << "  exit " << result.ExitCode << "\n  out: " << result.Out << "\n  err: " << result.Err << '\n';
    }

    const char* const GoodSource = "script A\n{\n    Int x = 1\n}\n";
    const char* const BadSource = "script P\n{\n    fn F()\n    {\n        if hp <= 0\n        {\n        }\n    }\n}\n";

    // 번역 표를 JBroc 과 같은 자리에서 읽어 기대 출력을 만든다. 출력이 라이브러리의 한 줄 서식과 같은지 보기 위해서다.
    std::string ExpectedLine(const std::filesystem::path& file, const char* locale)
    {
        DiagnosticMessages messages;
        const std::string directory = ToUtf8((TestExecutableDirectory() / L"Localization" / L"jbroc").native());
        Check(messages.Load(directory.c_str(), locale, "en-US"), "the copied translation tables load beside the test");
        const std::string path = ToUtf8(std::filesystem::absolute(file).lexically_normal().native());
        const SourceText source{ String(path), String(BadSource) };
        DiagnosticList diagnostics;
        Parse(source, diagnostics);
        Check(1 == diagnostics.GetCount(), "the bad probe has exactly one error");
        return FormatDiagnosticLine(path, diagnostics.GetItems()[0], messages).Std() + "\n";
    }

    void TestNumbersAreStableAndUnique()
    {
        const auto count = static_cast<std::uint16_t>(DiagnosticCode::Count);
        for (std::uint16_t first = 0; first < count; ++first)
        {
            const auto code = static_cast<DiagnosticCode>(first);
            const std::uint16_t number = GetDiagnosticNumber(code);
            const std::string key = GetDiagnosticKey(code);
            Check(0 != number, "every diagnostic has a number");
            if (0 == key.rfind("jbroc.lex.", 0))
            {
                Check(number >= 1001 && number <= 1999, "lexer diagnostics are numbered in the 1000s");
            }
            else
            {
                Check(0 == key.rfind("jbroc.parse.", 0), "every diagnostic is a lexer or parser one so far");
                Check(number >= 2001 && number <= 2999, "parser diagnostics are numbered in the 2000s");
            }
            for (std::uint16_t second = first + 1; second < count; ++second)
            {
                Check(GetDiagnosticNumber(static_cast<DiagnosticCode>(second)) != number, "no two diagnostics share a number");
            }
        }
        // 이미 내보낸 번호는 바뀌면 안 된다. 대표로 둘을 못박는다.
        Check(1001 == GetDiagnosticNumber(DiagnosticCode::UnexpectedCharacter), "UnexpectedCharacter stays JBC1001");
        Check(2010 == GetDiagnosticNumber(DiagnosticCode::MissingConditionParentheses), "MissingConditionParentheses stays JBC2010");
    }

    void TestTranslationTablesHoldDiagnosticAndCommandLineKeys()
    {
        DiagnosticMessages korean;
        DiagnosticMessages english;
        if (false == korean.Load("Localization/jbroc", "ko-KR", nullptr))
        {
            std::cout << "  [skip] no Localization/jbroc directory beside the test" << std::endl;
            return;
        }
        Check(english.Load("Localization/jbroc", "en-US", nullptr), "the English table loads");
        for (const char* key : CommandLineKeys)
        {
            const char* ko = korean.Find(key);
            const char* en = english.Find(key);
            if (nullptr == ko || nullptr == en)
            {
                std::cout << "  missing translation: " << key << '\n';
            }
            Check(nullptr != ko && nullptr != en, "every command line key is in both tables");
            Check(std::string(ko) != std::string(en), "the Korean and English command line messages differ");
        }
        const std::size_t expected = static_cast<std::size_t>(DiagnosticCode::Count) + std::size(CommandLineKeys);
        Check(korean.GetCount() == expected && english.GetCount() == expected,
            "the tables hold exactly the diagnostic and command line keys");
    }

    void TestTheExecutable()
    {
        const std::filesystem::path directory = TestExecutableDirectory();
        const std::filesystem::path executable = directory / L"JBroc.exe";
        if (false == std::filesystem::exists(executable))
        {
            std::cout << "  [skip] no JBroc.exe beside the test" << std::endl;
            return;
        }

        const std::filesystem::path scratch = std::filesystem::temp_directory_path() / L"JBroc_테스트_probe";
        std::filesystem::remove_all(scratch);
        std::filesystem::create_directories(scratch);
        const std::filesystem::path good = scratch / L"good.jscript";
        const std::filesystem::path bad = scratch / L"bad.jscript";
        const std::filesystem::path korean = scratch / L"적.jscript";
        WriteBytes(good, GoodSource);
        WriteBytes(bad, BadSource);
        WriteBytes(korean, BadSource);

        {
            const RunResult result = Run(executable, L"", scratch);
            Check(2 == result.ExitCode && result.Out.empty(), "no input is a usage error with nothing on standard output");
            Check(std::string::npos != result.Err.find("JBroc [--locale"), "no input prints the usage to standard error");
            Check(std::string::npos == result.Err.find("jbroc.cli."), "the usage is a translated message, not a key");
        }
        {
            const RunResult result = Run(executable, L"--help", scratch);
            Check(0 == result.ExitCode && result.Err.empty(), "--help succeeds quietly on standard error");
            Check(std::string::npos != result.Out.find("JBroc [--locale"), "--help prints the usage to standard output");
        }
        {
            const RunResult result = Run(executable, L"--bogus " + Quote(good), scratch);
            Check(2 == result.ExitCode && std::string::npos != result.Err.find("--bogus"), "an unknown option is named and refused");
        }
        {
            const RunResult result = Run(executable, Quote(good) + L" --locale", scratch);
            Check(2 == result.ExitCode && std::string::npos != result.Err.find("--locale"), "--locale without a value is refused");
        }
        {
            const RunResult result = Run(executable, L"--locale fr-FR " + Quote(good), scratch);
            Check(2 == result.ExitCode && std::string::npos != result.Err.find("fr-FR"), "an unsupported locale is named and refused");
        }
        {
            const RunResult result = Run(executable, Quote(scratch / L"missing.jscript"), scratch);
            Check(2 == result.ExitCode && std::string::npos != result.Err.find("missing.jscript"), "a missing file is named and refused");
        }
        {
            const RunResult result = Run(executable, Quote(good), scratch);
            if (0 != result.ExitCode || false == result.Out.empty() || false == result.Err.empty())
            {
                Print(result);
            }
            Check(0 == result.ExitCode && result.Out.empty() && result.Err.empty(), "a clean file succeeds with no output");
        }

        std::string koreanOut;
        {
            const RunResult result = Run(executable, Quote(bad), scratch);
            const std::string expected = ExpectedLine(bad, "ko-KR");
            if (result.Out != expected)
            {
                Print(result);
                std::cout << "  expected: " << expected;
            }
            Check(1 == result.ExitCode && result.Err.empty(), "a file with errors exits 1 and keeps standard error clean");
            Check(result.Out == expected, "the error line is the library's line with the Korean message by default");
            Check(std::string::npos != result.Out.find("(5,12): error JBC2010: "), "the line carries line, column and number");
            koreanOut = result.Out;

            std::smatch match;
            const std::string line = result.Out.substr(0, result.Out.size() - 1);
            const bool matched = std::regex_match(line, match, std::regex(MsCompilePattern));
            Check(matched, "the line matches the $msCompile problem matcher");
            Check(match[1].str() == ToUtf8(std::filesystem::absolute(bad).lexically_normal().native()),
                "the matcher reads the absolute path, which $msCompile requires");
            Check(match[2].str() == "5,12" && match[4].str() == "error" && match[5].str() == "JBC2010",
                "the matcher reads the location, severity and code");
        }
        {
            const RunResult result = Run(executable, L"--locale en-US " + Quote(bad), scratch);
            Check(1 == result.ExitCode && result.Out == ExpectedLine(bad, "en-US"), "--locale en-US gives the English message");
            Check(result.Out != koreanOut, "the English line differs from the Korean one");
        }
        {
            const RunResult result = Run(executable, Quote(korean), scratch);
            const std::string path = ToUtf8(std::filesystem::absolute(korean).lexically_normal().native());
            Check(1 == result.ExitCode && 0 == result.Out.rfind(path + "(5,12)", 0),
                "a Korean file name comes through the arguments and back out as UTF-8");
        }
        {
            const RunResult result = Run(executable, Quote(good) + L" " + Quote(scratch / L"missing.jscript") + L" " + Quote(bad), scratch);
            Check(2 == result.ExitCode, "a missing file outranks source errors in the exit code");
            Check(std::string::npos != result.Out.find("JBC2010") && std::string::npos != result.Err.find("missing.jscript"),
                "the other files are still compiled after one cannot be read");
        }
        {
            const RunResult result = Run(executable, L"--dump-tree " + Quote(good), scratch);
            Check(0 == result.ExitCode && 0 == result.Out.rfind("CompilationUnit\n  ScriptDeclaration A\n", 0), "--dump-tree prints the tree");
        }
        {
            // 번역 표가 없는 자리에 옮긴 JBroc 은 키로라도 알리고, 에러를 가르는 일은 그대로 한다.
            const std::filesystem::path alone = scratch / L"alone";
            std::filesystem::create_directories(alone);
            std::filesystem::copy_file(executable, alone / L"JBroc.exe", std::filesystem::copy_options::overwrite_existing);
            const RunResult result = Run(alone / L"JBroc.exe", Quote(bad), scratch);
            Check(1 == result.ExitCode, "without translation tables the exit code still reports the error");
            Check(std::string::npos != result.Out.find("JBC2010: jbroc.parse.missing_condition_parentheses"),
                "without translation tables the message is the key");
            Check(std::string::npos != result.Err.find("no diagnostic messages"), "the missing tables are reported once");
        }

        std::filesystem::remove_all(scratch);
    }
}

int RunScriptCompilerCommandLineTests()
{
    TestNumbersAreStableAndUnique();
    TestTranslationTablesHoldDiagnosticAndCommandLineKeys();
    TestTheExecutable();
    std::cout << "Script compiler command line tests passed.\n";
    return 0;
}
