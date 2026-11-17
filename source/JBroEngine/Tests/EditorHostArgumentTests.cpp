#include "TestCases.h"
#include <JBro/Editor/EditorHostArguments.h>
#include <JBro/Platform/WindowsPlatform.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// **에디터 실행 파일의 인자와 `.jproject` 연결**(D-336). 탐색기에서 두 번 누르면 `"<에디터>" --content-root "<폴더>" "<프로젝트>"` 가 돈다.
// 해석·연결의 모양·레지스트리 쓰기와 지우기를 따로 보고, 마지막에 진짜 에디터를 그 명령으로 띄워 한글 경로의 프로젝트가 열리는지 본다.
namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // 사람의 연결을 덮지 않는 시험의 자리다. 끝나면 통째로 지운다.
    constexpr const char* TestClasses = "Software\\JBroEngineTests\\Classes";

    void TestArgumentsAreParsed()
    {
        const char* none[] = {"JBroEditorHost.exe"};
        JBro::EditorHostArguments parsed = JBro::ParseEditorHostArguments(1, none);
        Check(parsed.IsValid() && parsed.projectFile.empty() && parsed.controlPort == JBro::EditorControlPort::DefaultPort,
            "no arguments opens no project on the default port");

        // 탐색기가 주는 모양이다. 경로는 UTF-8 바이트 그대로 온다(`게임`).
        const char* explorer[] = {"JBroEditorHost.exe", "--content-root", "C:\\Engine", "C:\\p\\\xea\xb2\x8c\xec\x9e\x84.jproject"};
        parsed = JBro::ParseEditorHostArguments(4, explorer);
        Check(parsed.IsValid() && parsed.projectFile == "C:\\p\\\xea\xb2\x8c\xec\x9e\x84.jproject" && parsed.contentRoot == "C:\\Engine",
            "a bare path after the content root is the project, bytes untouched");

        const char* named[] = {"JBroEditorHost.exe", "--project", "a.jproject", "--frames", "3", "--control-port", "0"};
        parsed = JBro::ParseEditorHostArguments(7, named);
        Check(parsed.IsValid() && parsed.projectFile == "a.jproject" && parsed.frameLimit == 3 && parsed.controlPort == 0,
            "the named options are read");

        const char* twice[] = {"JBroEditorHost.exe", "--project", "a.jproject", "b.jproject"};
        parsed = JBro::ParseEditorHostArguments(4, twice);
        Check(false == parsed.IsValid() && parsed.error.find("b.jproject") != JBro::String::npos, "a second project is an error");

        const char* dangling[] = {"JBroEditorHost.exe", "--project"};
        parsed = JBro::ParseEditorHostArguments(2, dangling);
        Check(false == parsed.IsValid() && parsed.error.find("--project") != JBro::String::npos, "an option without its value is an error");

        const char* unknown[] = {"JBroEditorHost.exe", "--fullscreen"};
        parsed = JBro::ParseEditorHostArguments(2, unknown);
        Check(false == parsed.IsValid() && parsed.error.find("--fullscreen") != JBro::String::npos, "an unknown option is an error");

        const char* port[] = {"JBroEditorHost.exe", "--control-port", "70000"};
        Check(false == JBro::ParseEditorHostArguments(3, port).IsValid(), "a port past 65535 is an error");

        const char* add[] = {"JBroEditorHost.exe", "--register-file-type"};
        parsed = JBro::ParseEditorHostArguments(2, add);
        Check(parsed.IsValid() && parsed.registerFileType && false == parsed.unregisterFileType, "registering is asked for");
        const char* both[] = {"JBroEditorHost.exe", "--register-file-type", "--unregister-file-type"};
        Check(false == JBro::ParseEditorHostArguments(3, both).IsValid(), "registering and removing at once is an error");

        const char* help[] = {"JBroEditorHost.exe", "--frames", "3", "-h"};
        Check(JBro::ParseEditorHostArguments(4, help).showHelp, "help is asked for");
    }

    // 연결의 명령은 이 실행 파일을 따옴표로 감싸고 파일 자리를 `"%1"` 로 둔다.
    void TestTheProjectFileTypeRunsThisExecutable()
    {
        JBro::WindowsPlatform platform;
        JBro::FileTypeAssociation desc;
        Check(JBro::MakeProjectFileType(platform, desc), "the executable knows its own path");
        const JBro::String executable = platform.GetExecutablePath();
        Check(executable.find('\\') != JBro::String::npos && executable.find('/') == JBro::String::npos,
            "the executable path uses the system separator");
        JBro::String expected = "\"";
        expected.append(executable);
        expected.append("\" \"%1\"");
        Check(desc.command == expected, "double-clicking runs this executable on the file");
        JBro::String icon = "\"";
        icon.append(executable);
        icon.append("\",0");
        Check(desc.icon == icon, "and the file shows this executable's icon");
        Check(desc.extension == ".jproject" && desc.progId == "JBroEngine.Project" && desc.classesKey == "Software\\Classes",
            "it is the .jproject type for the current user");
    }

    void TestTheRegistryRoundTrip()
    {
        JBro::WindowsPlatform platform;
        JBro::FileTypeAssociation desc;
        Check(JBro::MakeProjectFileType(platform, desc), "the executable knows its own path");
        desc.classesKey = TestClasses;
        platform.UnregisterFileType(desc);
        Check(platform.ReadFileTypeCommand(TestClasses, ".jproject").empty(), "the test starts with nothing");

        Check(platform.RegisterFileType(desc), "the file type is written");
        Check(platform.ReadFileTypeCommand(TestClasses, ".jproject") == desc.command, "and reads back as the same command");
        Check(platform.RegisterFileType(desc), "writing it again is fine");

        Check(platform.UnregisterFileType(desc), "the file type is removed");
        Check(platform.ReadFileTypeCommand(TestClasses, ".jproject").empty(), "and nothing opens it any more");
        Check(platform.UnregisterFileType(desc), "removing what is not there is not an error");

        // 사람이 확장자를 다른 프로그램으로 바꿔 두었으면 지우기는 이 프로그램의 것만 지운다.
        Check(platform.RegisterFileType(desc), "the file type is written again");
        JBro::FileTypeAssociation other = desc;
        other.progId = "Someone.Else";
        other.command = "\"C:\\other.exe\" \"%1\"";
        other.icon.clear();
        Check(platform.RegisterFileType(other), "another program takes the extension");
        Check(platform.UnregisterFileType(desc), "removing ours succeeds");
        Check(platform.ReadFileTypeCommand(TestClasses, ".jproject") == other.command, "and leaves the other program's link alone");

        JBro::FileTypeAssociation broken = desc;
        broken.extension = "jproject";
        Check(false == platform.RegisterFileType(broken), "an extension without its dot is refused");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\JBroEngineTests");
    }

    std::filesystem::path TestExecutableDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        Check(0 != length && length < MAX_PATH, "the test executable path fits the buffer");
        return std::filesystem::path(buffer).parent_path();
    }

    struct HostRun
    {
        DWORD exitCode = 0xFFFFFFFF;
        std::string output;
    };

    // 에디터를 띄워 끝날 때까지 기다린다. 표준 출력은 파일로 받는다 - 에디터는 출력이 이미 돌려져 있으면 콘솔에 붙지 않는다.
    HostRun RunHost(const std::wstring& commandLine, const std::filesystem::path& scratch)
    {
        static JBro::Int32 counter = 0;
        ++counter;
        const std::filesystem::path outPath = scratch / (L"host" + std::to_wstring(counter) + L".txt");
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(INVALID_HANDLE_VALUE != out, "the output capture file opens");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out;
        startup.hStdError = out;
        PROCESS_INFORMATION process{};
        std::wstring command = commandLine;
        // 탐색기와 같이 작업 폴더를 프로젝트 쪽(여기서는 시험의 임시 폴더)으로 준다. 기준 폴더는 명령이 준다.
        const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, scratch.c_str(), &startup, &process);
        CloseHandle(out);
        Check(0 != started, "the editor starts");
        HostRun result;
        const DWORD waited = WaitForSingleObject(process.hProcess, 120000);
        if (WAIT_OBJECT_0 != waited)
        {
            TerminateProcess(process.hProcess, 99);
        }
        GetExitCodeProcess(process.hProcess, &result.exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Check(WAIT_OBJECT_0 == waited, "the editor finishes within two minutes");
        std::ifstream file(outPath, std::ios::binary);
        result.output.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return result;
    }

    // **진짜 에디터가 두 번 누른 명령으로 한글 폴더의 프로젝트를 연다**(D-336). 좁은 `main` 이던 때는 `argv` 가 ANSI 로 와서 이 경로가 열리지 않았다.
    void TestTheEditorOpensAProjectGivenOnTheCommandLine()
    {
        namespace fs = std::filesystem;
        const fs::path host = TestExecutableDirectory() / L"JBroEditorHost.exe";
        Check(fs::is_regular_file(host), "the editor is built beside the tests");
        wchar_t temp[MAX_PATH]{};
        Check(0 != GetTempPathW(MAX_PATH, temp), "the temp folder is known");
        const fs::path root = fs::path(temp) / L"JBroEditorHostArgs" / L"\uD504\uB85C\uC81D\uD2B8 \uC2DC\uD5D8";
        std::error_code ignored;
        fs::remove_all(root.parent_path(), ignored);
        fs::create_directories(root / L"Assets", ignored);
        const fs::path project = root / L"\uAC8C\uC784.jproject";
        {
            std::ofstream file(project, std::ios::binary);
            file << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\n";
        }
        const std::wstring hostText = host.native();
        // 연결이 적는 명령(`"<에디터>" "%1"`)에 시험이 끝나게 하는 두 옵션만 더한다. 작업 폴더는 프로젝트 쪽이다 - 글자 표는 실행 파일 옆에서 찾는다.
        const std::wstring opened = L"\"" + hostText + L"\" --frames 5 --control-port 0 \"" + project.native() + L"\"";
        const HostRun run = RunHost(opened, root.parent_path());
        if (run.exitCode == 1)
        {
            std::cout << "  [skip] the editor could not initialize (no D3D12 device?); opening from the command line not verified\n";
            fs::remove_all(root.parent_path(), ignored);
            return;
        }
        if (run.exitCode != 0)
        {
            std::cout << "  editor exit " << run.exitCode << "\n  output: " << run.output << '\n';
        }
        Check(run.exitCode == 0, "the editor opens the project and exits cleanly");
        Check(run.output.find("the editor opened") != std::string::npos && run.output.find("2D") != std::string::npos,
            "it says which project it opened");

        const std::wstring missing = L"\"" + hostText + L"\" --frames 1 --control-port 0 \"" + (root / L"\uC5C6\uC74C.jproject").native() + L"\"";
        Check(RunHost(missing, root.parent_path()).exitCode == 2, "a project that is not there is the project exit code");
        const std::wstring bad = L"\"" + hostText + L"\" --nope";
        Check(RunHost(bad, root.parent_path()).exitCode == 64, "an unknown option is the usage exit code");
        fs::remove_all(root.parent_path(), ignored);
    }
}

JBro::Int32 RunEditorHostArgumentTests()
{
    try
    {
        JBRO_CASE(TestArgumentsAreParsed());
        JBRO_CASE(TestTheProjectFileTypeRunsThisExecutable());
        JBRO_CASE(TestTheRegistryRoundTrip());
        JBRO_CASE(TestTheEditorOpensAProjectGivenOnTheCommandLine());
    }
    catch (const std::exception&)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\JBroEngineTests");
        return 1;
    }
    std::cout << "editor host argument tests passed\n";
    return 0;
}
