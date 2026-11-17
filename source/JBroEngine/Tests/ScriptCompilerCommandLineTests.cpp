// JBroc 실행 파일을 실제로 띄워 본다(D-105).
//
// 종료 코드, 표준 출력, 표준 에러를 따로 받는다. 출력 한 줄은 VS Code 의 $msCompile 정규식(upstream
// src/vs/workbench/contrib/tasks/common/problemMatcher.ts 의 원문)에 걸려야 한다 - 편집기가 에러를 문제 패널로 받는 길이다.
// JBroc.exe 는 이 테스트 실행 파일 옆에 빌드된다(JBroTests 가 프로젝트로 참조한다).

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/DiagnosticMessages.h>
#include <JBro/ScriptCompiler/Parser.h>
#include "../Modules/JBroc/Source/StackGuard.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>

#include <Windows.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(JBro::Bool condition, const char* message)
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
        const JBro::Int32 size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<JBro::Int32>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<JBro::Int32>(text.size()), result.data(), size, nullptr, nullptr);
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
    RunResult Run(const std::filesystem::path& executable, const std::wstring& arguments, const std::filesystem::path& scratch,
        const std::filesystem::path& workingDirectory = std::filesystem::path())
    {
        static JBro::Int32 counter = 0;
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
            nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
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
        return FormatDiagnosticLine(path, source.GetText(), diagnostics.GetItems()[0], messages).Std() + "\n";
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
            Check(JBro::String(ko) != JBro::String(en), "the Korean and English command line messages differ");
        }
        for (const char* key : GetSemanticDiagnosticKeys())
        {
            const char* ko = korean.Find(key);
            const char* en = english.Find(key);
            Check(nullptr != ko && nullptr != en, "every semantic diagnostic key is in both tables");
            Check(JBro::String(ko) != JBro::String(en), "the Korean and English semantic messages differ");
        }
        const char* summaryKeys[] = {
            "jbroc.summary.syntax_errors", "jbroc.summary.field_required", "jbroc.summary.invalid_attribute",
            "jbroc.summary.invalid_attribute_argument", "jbroc.summary.invalid_initializer", "jbroc.summary.unsafe_edit",
            "jbroc.summary.changes_required", "jbroc.summary.unsupported_change", "jbroc.summary.initializer_type",
            "jbroc.summary.attributes_type", "jbroc.summary.attribute_shape", "jbroc.summary.attribute_argument_type",
            "jbroc.summary.version_required", "jbroc.summary.stale_version", "jbroc.summary.selection_required"
        };
        for (const char* key : summaryKeys)
        {
            const char* ko = korean.Find(key);
            const char* en = english.Find(key);
            Check(nullptr != ko && nullptr != en, "every summary editing message key is in both tables");
            Check(JBro::String(ko) != JBro::String(en), "the Korean and English summary editing messages differ");
        }
        const std::size_t expected = static_cast<std::size_t>(DiagnosticCode::Count)
            + std::size(CommandLineKeys) + GetSemanticDiagnosticKeys().Size() + std::size(summaryKeys);
        Check(korean.GetCount() == expected && english.GetCount() == expected,
            "the tables hold exactly the parser, lexer, semantic, command line and summary keys");
    }

    void TestProjectOutputPreservesUnchangedFiles()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_project_output_probe";
        fs::create_directories(scratch);
        const fs::path output = scratch / L"generated";
        const fs::path first = scratch / L"First.jscript";
        const fs::path second = scratch / L"Second.jscript";
        WriteBytes(first, "script First {\n public ref Second peer\n fn Read() -> Int { return peer.score }\n}\n");
        WriteBytes(second, "script Second {\n public ref First peer\n public Int score = 1\n}\n");
        const std::wstring arguments = L"--emit-project " + Quote(output) + L" --no-module-entry "
            + Quote(first) + L" " + Quote(second);
        const RunResult generated = Run(executable, arguments, scratch);
        if (generated.ExitCode != 0)
        {
            Print(generated);
        }
        Check(generated.ExitCode == 0, "project generation accepts reciprocal script refs in separate files");
        fs::path firstCpp;
        fs::path secondCpp;
        fs::path firstHeader;
        fs::path secondHeader;
        for (const auto& file : fs::directory_iterator(output))
        {
            const JBro::String contents = ReadBytes(file.path());
            if (file.path().extension() == L".h")
            {
                if (contents.find("class First :") != std::string::npos)
                {
                    firstHeader = file.path();
                }
                if (contents.find("class Second :") != std::string::npos)
                {
                    secondHeader = file.path();
                }
            }
            if (file.path().extension() == L".cpp")
            {
                if (contents.find("First::Read") != std::string::npos)
                {
                    firstCpp = file.path();
                }
                else secondCpp = file.path();
            }
        }
        Check(!firstCpp.empty() && !secondCpp.empty() && firstCpp != secondCpp,
            "each source has its own generated translation unit");
        const auto firstTime = fs::last_write_time(firstCpp);
        const auto secondTime = fs::last_write_time(secondCpp);
        Check(!firstHeader.empty() && !secondHeader.empty()
                && ReadBytes(firstHeader).find(secondHeader.filename().string()) == std::string::npos
                && ReadBytes(firstCpp).find(secondHeader.filename().string()) != std::string::npos,
            "script references need only forward declarations in headers and definitions in sources");
        Check(Run(executable, arguments, scratch).ExitCode == 0
                && fs::last_write_time(firstCpp) == firstTime && fs::last_write_time(secondCpp) == secondTime,
            "unchanged generated bytes preserve both timestamps");
        Check(Run(executable, L"--emit-project " + Quote(output) + L" --no-module-entry " + Quote(second) + L" " + Quote(first), scratch).ExitCode == 0
                && fs::last_write_time(firstCpp) == firstTime && fs::last_write_time(secondCpp) == secondTime,
            "source enumeration order does not change translation units");
        const auto firstHeaderTime = fs::last_write_time(firstHeader);
        const auto secondHeaderTime = fs::last_write_time(secondHeader);
        WriteBytes(second, "script Second {\n public ref First peer\n public Int score = 2\n}\n");
        Check(Run(executable, arguments, scratch).ExitCode == 0
                && fs::last_write_time(firstCpp) == firstTime && fs::last_write_time(secondCpp) != secondTime
                && fs::last_write_time(firstHeader) == firstHeaderTime && fs::last_write_time(secondHeader) == secondHeaderTime,
            "changing a field initializer changes only its source, preserving dependent headers and translation units");
        const auto changedSecondTime = fs::last_write_time(secondCpp);
        WriteBytes(second, "script Second {\n Int score =\n}\n");
        Check(Run(executable, arguments, scratch).ExitCode == 1
                && fs::last_write_time(firstCpp) == firstTime && fs::last_write_time(secondCpp) == changedSecondTime,
            "source diagnostics leave the previous generated project untouched");
        WriteBytes(second, "script Second {\n public ref First peer\n public Int score = 1\n fn Extra() -> Int { return 7 }\n}\n");
        Check(Run(executable, arguments, scratch).ExitCode == 0 && fs::last_write_time(firstCpp) == firstTime,
            "editing another source does not regenerate this translation unit");
        const fs::path unrelated = output / L"user-owned.txt";
        WriteBytes(unrelated, "keep");
        WriteBytes(first, "script First {}\n");
        Check(Run(executable, L"--emit-project " + Quote(output) + L" --no-module-entry " + Quote(first), scratch).ExitCode == 0
                && !fs::exists(secondCpp) && ReadBytes(unrelated) == "keep",
            "source removal deletes only previously owned generated artifacts");
        Check(Run(executable, L"--emit-project " + Quote(output) + L" --no-module-entry", scratch).ExitCode == 0
                && !fs::exists(firstCpp) && ReadBytes(unrelated) == "keep",
            "removing the last source leaves no stale compiled script");
        const fs::path escaped = scratch / L"escaped.txt";
        WriteBytes(escaped, "preserve");
        WriteBytes(output / L"JBroGenerated.files", "JBro generated files v1\n../escaped.txt\n");
        Check(Run(executable, L"--emit-project " + Quote(output) + L" --no-module-entry", scratch).ExitCode == 2
                && ReadBytes(escaped) == "preserve" && ReadBytes(unrelated) == "keep",
            "a tampered manifest cannot delete files outside the generated directory");
        WriteBytes(output / L"JBroGenerated.files", "JBro generated files v1\nJBroGenerated.h\n");
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
            const JBro::Bool matched = std::regex_match(line, match, std::regex(MsCompilePattern));
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
            // $msCompile 은 절대 경로만 받는다. 상대 경로를 주면 작업 폴더 기준의 절대 경로로, `..` 을 푼 모양으로 내야 한다.
            std::filesystem::create_directories(scratch / L"sub");
            const RunResult result = Run(executable, L"sub\\..\\bad.jscript", scratch, scratch);
            const std::string path = ToUtf8((scratch / L"bad.jscript").lexically_normal().native());
            if (0 != result.Out.rfind(path + "(5,12)", 0))
            {
                Print(result);
            }
            Check(1 == result.ExitCode && 0 == result.Out.rfind(path + "(5,12)", 0),
                "a relative path comes out absolute and normalized");
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

    // **식의 깊이는 언어 규칙이다(D-374).** 뿌리에서 잎까지 식 노드가 `MaxExpressionDepth` 겹이면 지나고 하나 더면 `JBC2015` 로
    // 거절한다. 전에는 상한이 없어 깊이 24 에서 `jbroc` 이 말없이 죽었고(Script QA 결함 3), 큰 스택으로 고친 뒤에는 MSVC 가
    // `??` 사슬 약 122 겹에서 C1061 로 멈췄다. 모양마다 경계를 재는 까닭은 파서가 겹을 두 길(노드 깊이·파서 재귀)로 세기 때문이다.
    std::string RepeatJoined(const char* piece, const char* separator, JBro::Int32 count)
    {
        std::string text;
        for (JBro::Int32 index = 0; index < count; ++index)
        {
            text += index == 0 ? "" : separator;
            text += piece;
        }
        return text;
    }

    std::string Nested(const char* open, const char* leaf, const char* close, JBro::Int32 count)
    {
        std::string text;
        for (JBro::Int32 index = 0; index < count; ++index)
        {
            text += open;
        }
        text += leaf;
        for (JBro::Int32 index = 0; index < count; ++index)
        {
            text += close;
        }
        return text;
    }

    // 깊이 `depth` 의 식을 모양마다 만든다. 셈은 식 노드의 수다: `1 + 1` 은 2, `(1)` 은 2, `-1` 은 2, `Id(1)` 은 2, `maybe ?? 0` 은 2.
    struct DepthShape
    {
        const char* Name;
        const char* Return;
        std::string (*Make)(JBro::Int32 depth);
    };

    const DepthShape DepthShapes[] = {
        {"plus", "Int", [](JBro::Int32 depth) { return RepeatJoined("1", " + ", depth); }},
        {"and", "Bool", [](JBro::Int32 depth) { return RepeatJoined("flag", " and ", depth); }},
        {"concat", "String", [](JBro::Int32 depth) { return RepeatJoined("text", " + ", depth); }},
        {"multiply", "Float", [](JBro::Int32 depth) { return RepeatJoined("1.0", " * ", depth); }},
        {"paren", "Int", [](JBro::Int32 depth) { return Nested("(", "1", ")", depth - 1); }},
        {"negate", "Int", [](JBro::Int32 depth) { return Nested("-", "1", "", depth - 1); }},
        {"not", "Bool", [](JBro::Int32 depth) { return Nested("not ", "flag", "", depth - 1); }},
        {"call", "Int", [](JBro::Int32 depth) { return Nested("Id(", "1", ")", depth - 1); }},
        {"coalesce", "Int", [](JBro::Int32 depth) { return RepeatJoined("maybe", " ?? ", depth - 1) + " ?? 0"; }},
    };

    void TestDeepExpressionsAreRejectedWithAPosition()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_deep_expression_probe";
        fs::remove_all(scratch);
        fs::create_directories(scratch);
        const JBro::Int32 limit = static_cast<JBro::Int32>(JBro::ScriptCompiler::MaxExpressionDepth.Get());
        Check(limit == 100, "the expression depth limit is the number the documents name");
        const auto run = [&](const DepthShape& shape, JBro::Int32 depth) {
            const fs::path source = scratch / (std::string(shape.Name) + std::to_string(depth) + ".jscript");
            WriteBytes(source, std::string("script Deep\n{\n    Bool flag = true\n    String text = \"a\"\n    ref Int maybe\n"
                "    fn Id(Int v) -> Int\n    {\n        return v\n    }\n    fn F() -> ") + shape.Return
                + "\n    {\n        return " + shape.Make(depth) + "\n    }\n}\n");
            return Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" --locale en-US " + Quote(source), scratch);
        };
        for (const DepthShape& shape : DepthShapes)
        {
            const RunResult atLimit = run(shape, limit);
            if (atLimit.ExitCode != 0)
            {
                std::cout << "  shape " << shape.Name << " depth " << limit << std::endl;
                Print(atLimit);
            }
            Check(atLimit.ExitCode == 0, "an expression exactly at the depth limit is analyzed and emitted");
            const JBro::Int32 overDepths[] = { limit + 1, JBro::Int32(60000) };
            for (const JBro::Int32 depth : overDepths)
            {
                const RunResult over = run(shape, depth);
                const Bool rejected = over.ExitCode == 1 && over.Out.find(".jscript(12,") != std::string::npos
                    && over.Out.find("error JBC2015: This expression nests too deeply. An expression can nest up to 100 levels.") != std::string::npos
                    && over.Err.find("internal error") == std::string::npos
                    && over.Out.find("error JBC") == over.Out.rfind("error JBC");
                if (false == rejected)
                {
                    std::cout << "  shape " << shape.Name << " depth " << depth << std::endl;
                    Print(over);
                }
                Check(rejected, "an expression one level past the limit, or far past it, is one JBC2015 on its source line and nothing else");
            }
        }
        // 빈 오른쪽을 가진 `??`·`and` 는 구문 오류 하나로 끝난다. 전에는 분석이 빈 자리의 노드를 읽어 Debug 에서 단언으로 죽었다.
        for (const char* broken : { "maybe ?? maybe ?? ", "flag and " })
        {
            const fs::path source = scratch / L"broken.jscript";
            WriteBytes(source, std::string("script Broken\n{\n    Bool flag = true\n    ref Int maybe\n    fn F() -> Int\n    {\n        return ")
                + broken + "\n    }\n}\n");
            const RunResult result = Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" " + Quote(source), scratch);
            if (result.ExitCode != 1)
            {
                Print(result);
            }
            Check(result.ExitCode == 1 && result.Out.find("error JBC2005") != std::string::npos,
                "a binary operator with nothing on its right is one syntax error, not a crash");
        }
        fs::remove_all(scratch);
    }

    // **블록은 함수 하나에서 32 겹까지다(D-376).** 함수 몸이 1 이고 `if`·`while`·`for`·`case` 몸이 하나씩 더한다. 한 겹 더면 `{` 자리에
    // `JBC2016` 하나다. 수만 겹도 파서가 재귀하지 않고 건너뛰어 곧바로 끝난다. `else if` 사슬은 겹으로 세지 않는다.
    void TestDeepBlocksAreRejectedWithAPosition()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_deep_block_probe";
        fs::remove_all(scratch);
        fs::create_directories(scratch);
        const JBro::Int32 limit = static_cast<JBro::Int32>(JBro::ScriptCompiler::MaxBlockDepth.Get());
        Check(limit == 32, "the block depth limit is the number the documents name");
        struct BlockShape
        {
            const char* Name;
            const char* Open;
            const char* Close;
        };
        const BlockShape shapes[] = {
            {"if", "if (flag) {\n", "}\n"},
            {"while", "while (count < 0) {\n", "}\n"},
            {"for", "for (item in items) {\n", "}\n"},
            {"switch", "switch (count) {\ncase 0 {\n", "}\n}\n"},
        };
        // 함수 몸 `{` 은 6 줄에 있고 겹 하나가 한 줄(switch 는 두 줄)이다. 32 번째 안쪽 블록의 `{` 가 진단 자리다.
        const auto run = [&](const BlockShape& shape, JBro::Int32 inner) {
            std::string body;
            for (JBro::Int32 index = 0; index < inner; ++index)
            {
                body += shape.Open;
            }
            body += "count += 1\n";
            for (JBro::Int32 index = 0; index < inner; ++index)
            {
                body += shape.Close;
            }
            const fs::path source = scratch / (std::string(shape.Name) + std::to_string(inner) + ".jscript");
            WriteBytes(source, "script Blocks\n{\n    Bool flag = true\n    Int count = 0\n    Array<Int> items\n    fn F()\n    {\n"
                + body + "    }\n}\n");
            return Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" --locale en-US " + Quote(source), scratch);
        };
        for (const BlockShape& shape : shapes)
        {
            const RunResult atLimit = run(shape, limit - 1);
            if (atLimit.ExitCode != 0)
            {
                std::cout << "  shape " << shape.Name << " at the limit" << std::endl;
                Print(atLimit);
            }
            Check(atLimit.ExitCode == 0, "blocks exactly at the depth limit (the body and 31 more) are analyzed and emitted");
            const JBro::Int32 overDepths[] = { limit, JBro::Int32(60000) };
            for (const JBro::Int32 inner : overDepths)
            {
                const RunResult over = run(shape, inner);
                const Bool rejected = over.ExitCode == 1
                    && over.Out.find("error JBC2016: These blocks nest too deeply. A function can nest blocks up to 32 levels.") != std::string::npos
                    && over.Out.find("error JBC") == over.Out.rfind("error JBC")
                    && over.Err.find("internal error") == std::string::npos;
                if (false == rejected)
                {
                    std::cout << "  shape " << shape.Name << " inner " << inner << std::endl;
                    Print(over);
                }
                Check(rejected, "one block past the limit, or far past it, is one JBC2016 and nothing else");
            }
        }
        // 위치: 함수 몸이 7 줄, 안쪽 블록 i 번째가 7 + i 줄이다. 32 번째 블록의 `{` 는 if 모양에서 39 줄 11 열이다.
        const RunResult placed = run(shapes[0], limit);
        Check(placed.Out.find("if32.jscript(39,11): error JBC2016") != std::string::npos, "JBC2016 points at the brace of the first block past the limit");
        // `else if` 는 사슬이라 겹이 아니다. 40 개도 지난다(D-376 - 사슬 길이는 사용자 결정 대기).
        std::string chain = "if (count == 0) {\ncount = 1\n}\n";
        for (JBro::Int32 index = 1; index < 40; ++index)
        {
            chain += "else if (count == " + std::to_string(index.Get()) + ") {\ncount = " + std::to_string(index.Get() + 1) + "\n}\n";
        }
        const fs::path chained = scratch / L"chain.jscript";
        WriteBytes(chained, "script Chain\n{\n    Int count = 0\n    fn F()\n    {\n" + chain + "    }\n}\n");
        const RunResult chainResult = Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" " + Quote(chained), scratch);
        if (chainResult.ExitCode != 0)
        {
            Print(chainResult);
        }
        Check(chainResult.ExitCode == 0, "an else-if chain of 40 is not counted as nested blocks");
        fs::remove_all(scratch);
    }

    // **넘침을 받는 길은 구성과 무관하게 지난다**(REVIEW R3-1). 식(D-374)과 블록(D-376)은 이제 상한으로 막히므로 `jbroc` 을 넘치게 하는
    // 입력은 `else if` 수만 개 같은 것뿐이고, Debug 도 1 분이 넘게 걸린다. 그래서 `jbroc` 진입점이 쓰는 그 함수를 작은 스택으로 부른다.
    // `stop` 에 닿으면 돌아온다. 닿지 않는 `stop`(음수)이면 스택이 넘칠 때까지 들어간다. 프레임마다 지역 배열을 두고 돌아온 뒤에 더해
    // 최적화가 재귀를 반복문으로 바꾸지 못하게 한다.
    JBro::Int32 RecurseUntil(JBro::Int32 depth, JBro::Int32 stop)
    {
        volatile char frame[256];
        frame[0] = static_cast<char>(depth.Get());
        if (stop == depth)
        {
            return frame[0];
        }
        return RecurseUntil(depth + 1, stop) + frame[0];
    }

    void TestTheStackGuardCatchesOverflow()
    {
        using JBro::CompilerOutput::GuardedRun;
        using JBro::CompilerOutput::RunOnLargeStack;
        const GuardedRun overflow = RunOnLargeStack([](void*) -> JBro::Int32 { return RecurseUntil(1, -1); }, nullptr,
            1024u * 1024u, 3);
        Check(overflow.StackOverflow && overflow.Result == 3, "a stack overflow inside the guard returns the internal error result");
        const GuardedRun deep = RunOnLargeStack([](void*) -> JBro::Int32 { RecurseUntil(1, 10000); return 42; }, nullptr,
            256u * 1024u * 1024u, 3);
        Check(false == deep.StackOverflow && deep.Result == 42, "10,000 frames fit in the reserved stack and return the body's result");
    }
}

namespace
{
    // **생성 단계의 오류도 자리를 말한다**(Script QA 결함 4). 전에는 `jbroc: C++ emit error: …` 한 줄이라 어느 파일 몇 줄인지 몰랐다.
    void TestEmitErrorsNameTheSourcePosition()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_emit_error_position_probe";
        fs::remove_all(scratch);
        fs::create_directories(scratch);
        // 빈 enum 은 이제 받으므로(D-377) 아직 생성기만 아는 꼴(버퍼와 길이를 받는 투영)로 자리를 본다.
        const fs::path source = scratch / L"Copy.jscript";
        WriteBytes(source, "script Copy\n{\n    ref Text2D label\n    fn F() -> String\n    {\n        return Text.CopyText(label!)\n    }\n}\n");
        const RunResult result = Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" " + Quote(source), scratch);
        if (result.ExitCode != 1)
        {
            Print(result);
        }
        Check(result.ExitCode == 1 && result.Err.find("Copy.jscript(6,16): ") != std::string::npos,
            "an emitter error carries the source file, line and column");
        fs::remove_all(scratch);
    }
}

namespace
{
    // **빈 enum 은 C++ 처럼 받고, 값으로 담는 필드가 돌아 자기 타입을 담으면 `JBC3048` 이다(D-377).** 전에는 둘 다 생성기만 알아서
    // 빌드에서야 위치 없는 영어 한 줄로 멈췄다(Script QA 결함 4). 순환은 자기 자신·두 타입·부모를 거치는 꼴을 보고,
    // `Array`·script 핸들·정적 필드는 사슬을 끊으므로 진단이 없어야 한다.
    void TestEmptyEnumsAndOwnedCycles()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_owned_cycle_probe";
        fs::remove_all(scratch);
        fs::create_directories(scratch);
        const auto run = [&](const char* name, const std::string& text) {
            const fs::path source = scratch / name;
            WriteBytes(source, text);
            return Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" --locale en-US " + Quote(source), scratch);
        };
        const RunResult empty = run("Empty.jscript",
            "enum Nothing\n{\n}\n\nscript UsesNothing\n{\n    Nothing value\n    fn Same(Nothing other) -> Bool\n    {\n        return value == other\n    }\n}\n");
        if (empty.ExitCode != 0)
        {
            Print(empty);
        }
        Check(empty.ExitCode == 0 && empty.Err.find("emit error") == std::string::npos, "an enum without members is analyzed and emitted");

        struct Cycle
        {
            const char* Name;
            const char* Text;
            const char* Expected;
        };
        const Cycle cycles[] = {
            {"Self.jscript", "class Node\n{\n    Node next\n}\n", "Self.jscript(3,10): error JBC3048: A field held by value leads back to its own type: Node.next -> Node."},
            {"Pair.jscript", "struct Left\n{\n    Right right\n}\n\nstruct Right\n{\n    ref Left left\n}\n",
                "Pair.jscript(3,11): error JBC3048: A field held by value leads back to its own type: Left.right -> Right."},
            {"Base.jscript", "class Base\n{\n    Derived child\n}\n\nclass Derived : Base\n{\n}\n",
                "Base.jscript(3,13): error JBC3048: A field held by value leads back to its own type: Base.child -> Derived."},
        };
        for (const Cycle& cycle : cycles)
        {
            const RunResult result = run(cycle.Name, cycle.Text);
            const Bool reported = result.ExitCode == 1 && result.Out.find(cycle.Expected) != std::string::npos
                && result.Err.find("emit error") == std::string::npos;
            if (false == reported)
            {
                std::cout << "  " << cycle.Name << std::endl;
                Print(result);
            }
            Check(reported, "an owned field cycle is JBC3048 at the field, in the analysis");
        }
        const RunResult pair = run("Pair.jscript", cycles[1].Text);
        Check(pair.Out.find("Pair.jscript(8,14): error JBC3048") != std::string::npos, "every field on the cycle is reported, the ref value one too");

        const RunResult broken = run("Broken.jscript",
            "class Tree\n{\n    Array<Tree> children\n    static Tree shared\n}\n\nscript Holder\n{\n    ref Holder other\n    Tree root\n}\n");
        if (broken.ExitCode != 0)
        {
            Print(broken);
        }
        Check(broken.ExitCode == 0, "an Array, a static field and a script handle do not make an owned cycle");
        fs::remove_all(scratch);
    }
}

namespace
{
    // **명령줄 진단의 열은 글자 단위다(D-379).** 전에는 UTF-8 바이트 열이라 한글 뒤의 자리가 편집기·빌드 결과와 어긋났다
    // (한글 셋 뒤의 이름이 글자로 24 열인데 30 열). 구문 진단·의미 진단·생성기 오류가 같은 단위다. BOM 은 열에 들지 않는다.
    void TestColumnsCountCharacters()
    {
        namespace fs = std::filesystem;
        const fs::path executable = TestExecutableDirectory() / L"JBroc.exe";
        const fs::path scratch = fs::temp_directory_path() / L"JBroc_column_probe";
        fs::remove_all(scratch);
        fs::create_directories(scratch);
        const std::string korean = "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4";
        struct Case
        {
            const char* Name;
            std::string Text;
            const char* Expected;
        };
        const Case cases[] = {
            {"Semantic.jscript", "script A\n{\n    fn F() -> String\n    {\n        return \"" + korean + "\" + missingName\n    }\n}\n",
                "Semantic.jscript(5,24): error JBC3004"},
            {"Syntax.jscript", "script B\n{\n    fn F() -> String\n    {\n        return \"" + korean + "\" + )\n    }\n}\n",
                "Syntax.jscript(5,24): error JBC2005"},
            {"Bom.jscript", "\xef\xbb\xbfscript C\n{\n    Int hp = \"" + korean + "\"\n}\n", "Bom.jscript(3,14): error JBC3003"},
            {"Emit.jscript", "script D\n{\n    ref Text2D label\n    fn F() -> String\n    {\n        return \"" + korean + "\" + Text.CopyText(label!)\n    }\n}\n",
                "Emit.jscript(6,"},
        };
        for (const Case& item : cases)
        {
            const fs::path source = scratch / item.Name;
            WriteBytes(source, item.Text);
            const RunResult result = Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" " + Quote(source), scratch);
            const Bool placed = (result.Out + result.Err).find(item.Expected) != std::string::npos;
            if (false == placed)
            {
                std::cout << "  " << item.Name << " expected " << item.Expected << std::endl;
                Print(result);
            }
            Check(placed, "a diagnostic after Korean text names the character column");
        }
        // 생성기 오류도 글자 열이다: `return "가나다" + Text.CopyText(…)` 의 투영 호출은 글자로 24 열.
        const RunResult emitted = Run(executable, L"--emit-project " + Quote(scratch / L"out") + L" " + Quote(scratch / L"Emit.jscript"), scratch);
        Check((emitted.Out + emitted.Err).find("Emit.jscript(6,24)") != std::string::npos, "an emitter error after Korean text names the character column");
        fs::remove_all(scratch);
    }
}

JBro::Int32 RunScriptCompilerCommandLineTests()
{
    TestEmitErrorsNameTheSourcePosition();
    TestEmptyEnumsAndOwnedCycles();
    TestColumnsCountCharacters();
    TestNumbersAreStableAndUnique();
    TestTranslationTablesHoldDiagnosticAndCommandLineKeys();
    TestTheExecutable();
    TestProjectOutputPreservesUnchangedFiles();
    TestDeepExpressionsAreRejectedWithAPosition();
    TestDeepBlocksAreRejectedWithAPosition();
    TestTheStackGuardCatchesOverflow();
    std::cout << "Script compiler command line tests passed.\n";
    return 0;
}
