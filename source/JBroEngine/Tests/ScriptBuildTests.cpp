#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Editor/ScriptBuild.h>
#include <JBro/Editor/ScriptProject.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/NameTable.h>

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// 에디터가 스크립트 프로젝트를 빌드하는 길(cpp-script-plan §3.4, D-267)을 본다. 자식 프로세스 API, MSBuild 로그 읽기, 편집기 명령,
// 그리고 MSBuild 가 있으면 에디터가 오류를 넣은 스크립트를 실제로 빌드해 그 줄을 찾는지까지 본다.

namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    bool Contains(const String& text, const char* piece)
    {
        return text.find(piece) != String::npos;
    }

    std::string ReadAll(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    fs::path FromUtf8(const String& text)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.c_str()), text.size()));
    }

    fs::path FreshFolder(const fs::path& name)
    {
        const fs::path root = fs::temp_directory_path() / fs::path(u8"JBro스크립트빌드") / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root, ignored);
        return root;
    }

    String CmdLine(const char* arguments)
    {
        char system[MAX_PATH] = {};
        GetSystemDirectoryA(system, MAX_PATH);
        String command = "\"";
        command += system;
        command += "\\cmd.exe\" ";
        command += arguments;
        return command;
    }

    ProcessStatus WaitFor(WindowsPlatform& platform, const ChildProcess& process, std::int32_t& exitCode, int milliseconds)
    {
        ProcessStatus status = platform.PollProcess(process, exitCode);
        for (int waited = 0; status == ProcessStatus::Running && waited < milliseconds; waited += 20)
        {
            Sleep(20);
            status = platform.PollProcess(process, exitCode);
        }
        return status;
    }

    // 띄우고, 기다리지 않고 묻고, 끝난 코드와 출력 파일을 받는다. 작업 폴더와 출력 파일은 한글 경로다.
    void TestAChildProcessRunsAndReportsItsExit()
    {
        const fs::path root = FreshFolder(u8"프로세스");
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");

        const String output = Utf8(root / u8"출력.txt");
        ChildProcess process = platform.StartProcess(CmdLine("/c cd & echo spoken& exit 3").c_str(), Utf8(root).c_str(), output.c_str());
        Check(process.process != nullptr, "the process starts");
        std::int32_t exitCode = -1;
        Check(WaitFor(platform, process, exitCode, 10000) == ProcessStatus::Exited, "and exits");
        Check(exitCode == 3, "with the code it chose");
        platform.CloseProcess(process);
        Check(process.process == nullptr && process.group == nullptr, "closing forgets the handles");

        const std::string text = ReadAll(FromUtf8(output));
        Check(text.find("spoken") != std::string::npos, "what it printed is in the output file");
        // `cd` 는 작업 폴더를 찍는다. 콘솔 코드 페이지로 찍혀 한글은 깨지니 끝의 ASCII 만 본다 - 폴더를 받았다는 것은 앞의 경로로 안다.
        Check(text.find("JBro") != std::string::npos, "it ran in the working folder it was given");

        std::int32_t untouched = 7;
        Check(platform.PollProcess(process, untouched) == ProcessStatus::Invalid && untouched == 7,
            "a closed process is invalid and leaves the code alone");
        Check(platform.StartProcess("", nullptr, nullptr).process == nullptr, "an empty command starts nothing");
        Check(platform.StartProcess("\"Z:/no/such/tool.exe\"", nullptr, nullptr).process == nullptr,
            "a missing program starts nothing");
        Check(platform.StartProcess(CmdLine("/c exit 0").c_str(), nullptr, Utf8(root / "missing" / "out.txt").c_str()).process == nullptr,
            "an output file that cannot be made starts nothing");
        platform.Shutdown();
    }

    std::uintmax_t SizeOf(const fs::path& path)
    {
        std::error_code ignored;
        const std::uintmax_t size = fs::file_size(path, ignored);
        return ignored ? 0 : size;
    }

    // **아직 도는 것을 닫으면 그것이 띄운 것까지 끝난다**(`ping` 은 `cmd` 의 자식이다). 끝난 뒤에 닫으면 남은 것을 건드리지 않는다.
    // 둘 다 출력 파일이 자라는지로 본다 - `ping` 은 1 초마다 한 줄을 쓴다.
    void TestClosingARunningProcessEndsWhatItStarted()
    {
        const fs::path root = FreshFolder("group");
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");

        const fs::path killedOutput = root / "killed.txt";
        ChildProcess running = platform.StartProcess(CmdLine("/c ping -n 30 127.0.0.1").c_str(), nullptr, Utf8(killedOutput).c_str());
        Check(running.process != nullptr, "the long process starts");
        Sleep(1500);
        std::int32_t exitCode = 0;
        Check(platform.PollProcess(running, exitCode) == ProcessStatus::Running, "and is still running");
        platform.CloseProcess(running);
        Sleep(300);
        const std::uintmax_t afterClose = SizeOf(killedOutput);
        Sleep(2500);
        Check(SizeOf(killedOutput) == afterClose, "closing a running process ends the program it started too");

        // `start /b` 로 띄운 `ping` 은 `cmd` 가 끝난 뒤에도 돈다. 스스로 끝난 것을 닫을 때는 남은 것을 끝내지 않는다
        // (빌드가 띄운 PDB 서버처럼 다른 빌드가 함께 쓰는 것).
        const fs::path keptOutput = root / "kept.txt";
        ChildProcess finished = platform.StartProcess(CmdLine("/c start /b ping -n 5 127.0.0.1").c_str(), nullptr, Utf8(keptOutput).c_str());
        Check(finished.process != nullptr, "the short process starts");
        Check(WaitFor(platform, finished, exitCode, 10000) == ProcessStatus::Exited, "and exits on its own");
        platform.CloseProcess(finished);
        const std::uintmax_t afterExit = SizeOf(keptOutput);
        Sleep(2500);
        Check(SizeOf(keptOutput) > afterExit, "what it left running keeps running after a normal exit");
        Sleep(2500);
        platform.Shutdown();
    }

    void TestAnEnvironmentVariableIsRead()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        SetEnvironmentVariableW(L"JBRO_BUILD_PROBE", L"\uac12 value");
        Check(platform.ReadEnvironmentVariable("JBRO_BUILD_PROBE") == "\xea\xb0\x92 value", "a set variable reads back as UTF-8");
        SetEnvironmentVariableW(L"JBRO_BUILD_PROBE", nullptr);
        Check(platform.ReadEnvironmentVariable("JBRO_BUILD_PROBE").empty(), "an unset variable is empty");
        Check(platform.ReadEnvironmentVariable("").empty() && platform.ReadEnvironmentVariable(nullptr).empty(), "no name is empty");
        platform.Shutdown();
    }

    // MSBuild 파일 로거의 줄 모양들이다. 실제 로그에서 옮겼다(한국어 VS 2026, 한글 경로).
    void TestTheBuildLogIsRead()
    {
        const char* const compile =
            "F:\\\xed\x94\x84\xeb\xa1\x9c\xec\xa0\x9d\xed\x8a\xb8\\Contents\\Scripts\\Player.cpp(12,5): error C2065: 'spd': "
            "\xec\x84\xa0\xec\x96\xb8\xeb\x90\x98\xec\xa7\x80 \xec\x95\x8a\xec\x9d\x80 \xec\x8b\x9d\xeb\xb3\x84\xec\x9e\x90\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4. "
            "[F:\\\xed\x94\x84\xeb\xa1\x9c\xec\xa0\x9d\xed\x8a\xb8\\Contents\\GameScript.vcxproj]";
        ScriptBuild::Diagnostic diagnostic;
        Check(ScriptBuild::ParseDiagnosticLine(compile, std::strlen(compile), diagnostic), "a compile error is a diagnostic");
        Check(diagnostic.file == "F:\\\xed\x94\x84\xeb\xa1\x9c\xec\xa0\x9d\xed\x8a\xb8\\Contents\\Scripts\\Player.cpp",
            "its file is the path before the parenthesis");
        Check(diagnostic.line == 12 && diagnostic.column == 5, "with its line and column");
        Check(diagnostic.isError && diagnostic.code == "C2065", "it is an error with its code");
        Check(diagnostic.message == "'spd': \xec\x84\xa0\xec\x96\xb8\xeb\x90\x98\xec\xa7\x80 \xec\x95\x8a\xec\x9d\x80 "
                                    "\xec\x8b\x9d\xeb\xb3\x84\xec\x9e\x90\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.",
            "the message keeps its colons and drops the project in brackets");

        const char* const warning = "  F:\\a\\Player.h(3): warning C4100: 'x': unreferenced parameter\r";
        Check(ScriptBuild::ParseDiagnosticLine(warning, std::strlen(warning), diagnostic), "a warning is a diagnostic");
        Check(false == diagnostic.isError && diagnostic.file == "F:\\a\\Player.h" && diagnostic.line == 3 && diagnostic.column == 0,
            "a warning without a column has line only");
        Check(diagnostic.message == "'x': unreferenced parameter", "and no trailing carriage return");

        const char* const fatal = "F:\\a\\Player.cpp(1,10): fatal error C1083: cannot open include file: 'Nope.h'";
        Check(ScriptBuild::ParseDiagnosticLine(fatal, std::strlen(fatal), diagnostic) && diagnostic.isError
                && diagnostic.code == "C1083" && diagnostic.line == 1,
            "a fatal compile error is an error");

        const char* const linker = "LINK : fatal error LNK1104: cannot open file 'JBroCore.lib' [F:\\a\\GameScript.vcxproj]";
        Check(ScriptBuild::ParseDiagnosticLine(linker, std::strlen(linker), diagnostic), "a linker error is a diagnostic");
        Check(diagnostic.file.empty() && diagnostic.line == 0 && diagnostic.code == "LNK1104"
                && diagnostic.message == "cannot open file 'JBroCore.lib'",
            "with no file");

        const char* const unresolved = "Player.obj : error LNK2019: unresolved external symbol \"void Foo(void)\"";
        Check(ScriptBuild::ParseDiagnosticLine(unresolved, std::strlen(unresolved), diagnostic) && diagnostic.file.empty()
                && diagnostic.code == "LNK2019",
            "an object file is not a source file to open");

        const char* const plain[] = {
            "  Player.cpp",
            "  GameScript.vcxproj -> F:\\a\\x64\\Debug\\GameScript.dll",
            "Build succeeded.",
            "    0 Warning(s)",
            "note: see declaration of 'Foo'",
            "",
        };
        for (const char* line : plain)
        {
            Check(false == ScriptBuild::ParseDiagnosticLine(line, std::strlen(line), diagnostic), "an ordinary line is not a diagnostic");
        }

        // BOM 과 CRLF, 병렬 빌드가 같은 진단을 두 번 적는 것. BOM 바로 뒤의 첫 줄이 진단이어도 그 경로가 BOM 을 달지 않는다.
        std::string log = "\xef\xbb\xbf";
        log += compile;
        log += "\r\n  Player.cpp\r\n";
        log += warning;
        log += "\n";
        log += compile;
        log += "\r\n";
        log += linker;
        Array<ScriptBuild::Diagnostic> diagnostics;
        diagnostics.Add(ScriptBuild::Diagnostic());
        ScriptBuild::ParseLog(log.data(), log.size(), diagnostics);
        Check(diagnostics.Size() == 3, "the log gives each diagnostic once and starts from empty");
        Check(diagnostics[0].code == "C2065" && diagnostics[1].code == "C4100" && diagnostics[2].code == "LNK1104",
            "in the order they were written");
        Check(diagnostics[0].file == "F:\\\xed\x94\x84\xeb\xa1\x9c\xec\xa0\x9d\xed\x8a\xb8\\Contents\\Scripts\\Player.cpp",
            "the first path does not carry the BOM");
        ScriptBuild::ParseLog("\xef\xbb\xbf", 3, diagnostics);
        Check(diagnostics.IsEmpty(), "a log with only a BOM has nothing");
    }

    void TestTheCommandsAreQuoted()
    {
        const String build = ScriptBuild::MakeBuildCommand("C:/VS/MSBuild.exe", "F:/\xed\x95\x9c/GameScript.vcxproj", "Debug", "F:/\xed\x95\x9c/x64/ScriptBuild.log");
        Check(build.find("\"C:/VS/MSBuild.exe\" \"F:/\xed\x95\x9c/GameScript.vcxproj\" ") == 0, "the tool and the project are quoted");
        Check(Contains(build, " -p:Configuration=Debug ") && Contains(build, " -p:Platform=x64 "), "for the configuration and x64");
        Check(Contains(build, " -nr:false "), "without leaving build nodes behind the editor");
        Check(Contains(build, "\"-flp:LogFile=F:/\xed\x95\x9c/x64/ScriptBuild.log;Encoding=UTF-8;Verbosity=minimal\""),
            "the diagnostics go to a UTF-8 log, quoted for the Korean path");

        Check(ScriptBuild::MakeOpenAtLineCommand(ScriptBuild::EditorKind::VisualStudio, "C:/VS/devenv.exe", "F:/a b/P.cpp", 12)
                == "\"C:/VS/devenv.exe\" /Edit \"F:/a b/P.cpp\" /Command \"Edit.GoTo 12\"",
            "Visual Studio opens the file in the running window and goes to the line");
        Check(ScriptBuild::MakeOpenAtLineCommand(ScriptBuild::EditorKind::VisualStudioCode, "C:/Code/Code.exe", "F:/a b/P.cpp", 7)
                == "\"C:/Code/Code.exe\" -g \"F:/a b/P.cpp:7\"",
            "VS Code takes the line after a colon");
        Check(Contains(ScriptBuild::MakeOpenAtLineCommand(ScriptBuild::EditorKind::VisualStudioCode, "C:/Code/Code.exe", "F:/P.cpp", 0), ":1\""),
            "a diagnostic with no line opens at the first line");
        Check(ScriptBuild::MakeOpenAtLineCommand(ScriptBuild::EditorKind::System, "C:/x.exe", "F:/P.cpp", 3).empty(),
            "the default app has no command, the shell opens it");
        Check(ScriptBuild::MakeOpenAtLineCommand(ScriptBuild::EditorKind::VisualStudio, "", "F:/P.cpp", 3).empty(),
            "an editor that was not found has no command");

        for (std::size_t index = 0; index < static_cast<std::size_t>(ScriptBuild::EditorKind::Count); ++index)
        {
            const ScriptBuild::EditorKind kind = static_cast<ScriptBuild::EditorKind>(index);
            ScriptBuild::EditorKind parsed = ScriptBuild::EditorKind::Count;
            Check(ScriptBuild::ParseEditorKind(ScriptBuild::EditorKindName(kind), parsed) && parsed == kind, "each editor name reads back");
        }
        ScriptBuild::EditorKind unchanged = ScriptBuild::EditorKind::VisualStudioCode;
        Check(false == ScriptBuild::ParseEditorKind("Notepad", unchanged) && false == ScriptBuild::ParseEditorKind(nullptr, unchanged)
                && unchanged == ScriptBuild::EditorKind::VisualStudioCode,
            "an unknown name is refused and leaves the value");
    }

    void TestTheToolsAreFound()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path scratch = FreshFolder("tools");
        const String msbuild = ScriptBuild::LocateMSBuild(platform, Utf8(scratch).c_str());
        if (msbuild.empty())
        {
            std::cout << "  [skip] no MSBuild on this machine; finding it not verified" << std::endl;
        }
        else
        {
            Check(fs::exists(FromUtf8(msbuild)) && FromUtf8(msbuild).filename() == "MSBuild.exe", "MSBuild is found as the program itself");
        }
        Check(ScriptBuild::LocateMSBuild(platform, "").empty() || false == platform.ReadEnvironmentVariable("VSINSTALLDIR").empty(),
            "without a scratch folder only the developer prompt can say where it is");
        const String code = ScriptBuild::LocateVisualStudioCode(platform);
        Check(code.empty() || fs::exists(FromUtf8(code)), "VS Code is found only where it is");
        platform.Shutdown();
    }

    // 에디터 설정은 사용자별 설정 파일에 남는다.
    void TestTheScriptEditorIsRemembered()
    {
        const fs::path root = FreshFolder("preferences");
        const String path = Utf8(root / "EditorPreferences.yaml");
        {
            EditorApplication editor;
            EditorApplicationConfig config;
            config.windowVisible = false;
            config.preferencesPath = path.c_str();
            if (false == editor.Initialize(config))
            {
                std::cout << "  [skip] no D3D12 device; remembering the script editor not verified" << std::endl;
                return;
            }
            Check(editor.GetScriptEditor() == ScriptBuild::EditorKind::VisualStudio, "Visual Studio is the default");
            editor.SetScriptEditor(ScriptBuild::EditorKind::VisualStudioCode);
            editor.Shutdown();
        }
        Check(ReadAll(FromUtf8(path)).find("ScriptEditor: VisualStudioCode") != std::string::npos, "the choice is written");
        {
            EditorApplication editor;
            EditorApplicationConfig config;
            config.windowVisible = false;
            config.preferencesPath = path.c_str();
            Check(editor.Initialize(config), "the editor starts again");
            Check(editor.GetScriptEditor() == ScriptBuild::EditorKind::VisualStudioCode, "and reads the choice back");
            editor.Shutdown();
        }
    }

    // 되돌리기 기록에 한 칸을 남기는 것뿐인 커맨드다.
    class NoteCommand final : public EditorCommand
    {
    public:
        const char* GetName() const override
        {
            return "Note";
        }
        bool Execute() override
        {
            return true;
        }
        void Undo() override
        {
        }
        void Redo() override
        {
        }
    };

    bool BuildAndWait(EditorApplication& editor)
    {
        using State = EditorApplication::ScriptBuildState;
        if (false == editor.BuildScripts())
        {
            return false;
        }
        Check(editor.GetScriptBuildState() == State::Running, "the build runs");
        Check(false == editor.BuildScripts(), "a second build does not start while one runs");
        // 에디터는 기다리지 않는다. 프레임을 돌리는 동안 끝난다.
        for (int frame = 0; frame < 6000 && editor.GetScriptBuildState() == State::Running; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor keeps ticking while the scripts build");
            Sleep(30);
        }
        return true;
    }

    // 에디터를 거친 길: 스크립트를 만들고, 오류를 넣어 빌드하면 그 줄이 오고, 고치면 빌드가 된다.
    void TestTheEditorBuildsTheScriptsAndFindsTheErrorLine()
    {
        using State = EditorApplication::ScriptBuildState;
        const fs::path root = FreshFolder(u8"에디터");
        fs::create_directories(root / "Contents" / "Assets");
        const fs::path projectPath = root / "Built.jproject";
        std::ofstream(projectPath, std::ios::binary) <<
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Contents/Assets\n"
            "ScriptSourceDirectory: Contents\n"
            "ScriptOutputLibraryPath: x64/Debug/GameScript.dll\n"
            "Build:\n"
            "  ProductName: Built\n";

        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; building from the editor not verified" << std::endl;
            return;
        }
        ProjectFileError error;
        Check(editor.OpenProjectFile(Utf8(projectPath).c_str(), error), "the project opens");
        Check(false == editor.BuildScripts() && editor.GetScriptBuildState() == State::Idle,
            "a project without a script project has nothing to build");
        Check(false == editor.OpenScriptDiagnostic(0), "there is no diagnostic to open");

        WindowsPlatform probe;
        JMemoryContext memory;
        Check(probe.Initialize(memory), "the platform initializes");
        const bool haveMSBuild = false == ScriptBuild::LocateMSBuild(probe, Utf8(FreshFolder("tools2")).c_str()).empty();
        probe.Shutdown();
        if (false == haveMSBuild)
        {
            std::cout << "  [skip] no MSBuild on this machine; building from the editor not verified" << std::endl;
            editor.Shutdown();
            return;
        }

        Array<ScriptProject::FieldSpec> fields;
        fields.Add({String("Speed"), ScriptProject::FieldType::Float});
        String failure;
        Check(false == editor.CreateScript("", "Player", fields, failure).empty(), "the script is created");
        const fs::path source = root / "Contents" / "Scripts" / "Player.cpp";
        // UI 를 켜 두어 빌드하는 동안과 끝난 뒤의 프레임이 `빌드 결과` 패널을 진단과 함께 그린다.
        Check(editor.EnableEditorUi({64, 48}), "the editor UI turns on");

        // 없는 이름을 쓰는 줄을 끝에 붙인다. 그 줄 번호가 진단에 와야 한다.
        std::string text = ReadAll(source);
        std::uint32_t brokenLine = 1;
        for (char c : text)
        {
            brokenLine += c == '\n' ? 1 : 0;
        }
        if (false == text.empty() && text.back() != '\n')
        {
            ++brokenLine;
            text += "\r\n";
        }
        std::ofstream(source, std::ios::binary) << text << "static int broken = notDeclaredAnywhere;\r\n";

        Check(BuildAndWait(editor), "the broken build starts");
        Check(editor.GetScriptBuildState() == State::Failed, "a script with an error fails to build");
        bool found = false;
        for (const ScriptBuild::Diagnostic& diagnostic : editor.GetScriptDiagnostics())
        {
            std::error_code ignored;
            found = found || (diagnostic.isError && diagnostic.code == "C2065" && diagnostic.line == brokenLine
                && fs::equivalent(FromUtf8(diagnostic.file), source, ignored));
        }
        Check(found, "the error points at the broken line of the script under its Korean path");
        Check(editor.GetNotifications().GetLastLevel() == NotificationLevel::Error, "the failure is announced");
        Check(editor.FindPanel("BuildResults") != nullptr && editor.FindPanel("BuildResults")->IsOpen(), "the build results are shown");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor draws the build results");
        }

        std::ofstream(source, std::ios::binary) << text;
        Check(BuildAndWait(editor), "the fixed build starts");
        Check(editor.GetScriptBuildState() == State::Succeeded, "the fixed script builds");
        for (const ScriptBuild::Diagnostic& diagnostic : editor.GetScriptDiagnostics())
        {
            Check(false == diagnostic.isError, "and leaves no error behind");
        }
        // `JBro.GameScript.props` 가 산출물을 `Contents` 옆의 `x64` 에 둔다 - 로그를 두는 자리와 같다.
        Check(fs::exists(root / "x64" / "Debug" / "GameScript.dll"), "the DLL is written next to the project");
        Check(editor.GetNotifications().GetLastLevel() == NotificationLevel::Success, "the success is announced");
        // 빌드가 성공하면 곧바로 싣는다(D-268). 프로젝트를 열 때는 DLL 이 없었다.
        Check(editor.IsScriptModuleLoaded(), "the new library is loaded without reopening the project");

        // **필드를 더해 다시 빌드해도 붙은 스크립트와 그 값이 이어진다**(cpp-script-plan §3.5 의 완료 조건).
        Canvas* canvas = editor.GetCanvas();
        Object::GameObject* object = canvas->CreateObject("Player");
        GameScriptBase* player = canvas->AttachScript(object, "Player");
        Check(player != nullptr, "the built script attaches");
        const auto field = [](const char* name) -> const PropertyInfo* {
            const PropertyTable* table = PropertyRegistry::Lookup("Player");
            for (std::uint32_t index = 0; table != nullptr && index < table->count; ++index)
            {
                if (std::strcmp(NameTable::Get().Resolve(table->properties[index].name), name) == 0)
                {
                    return &table->properties[index];
                }
            }
            return nullptr;
        };
        Check(field("Speed") != nullptr && field("Jump") == nullptr, "the first build has only its speed");
        *static_cast<float*>(field("Speed")->Address(player)) = 7.0f;
        const InstanceId playerId = player->GetInstanceId();
        const fs::path header = root / "Contents" / "Scripts" / "Player.h";
        std::string declaration = ReadAll(header);
        const std::size_t speedLine = declaration.find("    JBRO_FIELD(float, Speed) = 0.0f;\r\n");
        Check(speedLine != std::string::npos, "the header declares the speed field");
        declaration.insert(speedLine, "    JBRO_FIELD(float, Jump) = 9.0f;\r\n");
        std::ofstream(header, std::ios::binary) << declaration;
        const std::uint64_t generation = editor.GetScriptModule()->GetGeneration();
        Check(editor.GetCommands().Execute(MakeOwnerPtr<NoteCommand>()), "an edit goes into the history");
        Check(BuildAndWait(editor), "the build with a new field starts");
        Check(editor.GetScriptBuildState() == State::Succeeded, "and succeeds");
        Check(editor.GetScriptModule()->GetGeneration() != generation, "the library is swapped");
        Array<GameScriptBase*> scripts;
        canvas->CollectScripts(scripts);
        Check(scripts.Size() == 1 && scripts[0]->GetInstanceId() == playerId, "the script is still on its object");
        Check(field("Jump") != nullptr && *static_cast<const float*>(field("Jump")->ConstAddress(scripts[0])) == 9.0f,
            "the new field is there with its default");
        Check(*static_cast<const float*>(field("Speed")->ConstAddress(scripts[0])) == 7.0f, "and the old one kept its value");
        // 필드가 늘었다. 순번으로 필드를 가리키는 편집이 엉뚱한 필드를 되돌리지 않게 기록을 비운다.
        Check(editor.GetCommands().GetUndoCount() == 0, "a change in the fields clears the undo history");

        editor.ClearScriptDiagnostics();
        Check(editor.GetScriptDiagnostics().IsEmpty() && editor.GetScriptBuildState() == State::Idle, "clearing empties the results");

        // 도는 빌드는 프로젝트를 닫으면 끝난다.
        Check(editor.BuildScripts(), "another build starts");
        editor.CloseProject();
        Check(editor.GetScriptBuildState() != State::Running, "closing the project stops it");
        editor.Shutdown();
    }
}

int RunScriptBuildTests()
{
    TestAChildProcessRunsAndReportsItsExit();
    TestClosingARunningProcessEndsWhatItStarted();
    TestAnEnvironmentVariableIsRead();
    TestTheBuildLogIsRead();
    TestTheCommandsAreQuoted();
    TestTheToolsAreFound();
    TestTheScriptEditorIsRemembered();
    TestTheEditorBuildsTheScriptsAndFindsTheErrorLine();
    std::cout << "Script build tests passed.\n";
    return 0;
}
