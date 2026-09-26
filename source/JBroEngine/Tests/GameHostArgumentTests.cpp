#include <JBro/Host/GameHostArguments.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // **게임 호스트의 인자(D-115).** 두 키만 알고, 모르는 것과 값 없는 것은 오류다 - 오타가 빈 프로젝트로 뜨면 안 된다.
    void TestArgumentsAreParsedStrictly()
    {
        const char* none[] = {"JBroGameHost.exe"};
        JBro::GameHostArguments parsed = JBro::ParseGameHostArguments(1, none);
        Check(parsed.IsValid() && parsed.projectFile.empty() && parsed.canvasFile.empty(), "no arguments is an empty project");

        const char* both[] = {"JBroGameHost.exe", "--project", "C:/p/\xea\xb2\x8c\xec\x9e\x84.jproject", "--canvas", "Scenes/a.jcanvas"};
        parsed = JBro::ParseGameHostArguments(5, both);
        Check(parsed.IsValid() && parsed.projectFile == "C:/p/\xea\xb2\x8c\xec\x9e\x84.jproject"
                && parsed.canvasFile == "Scenes/a.jcanvas",
            "both keys are read in order, bytes untouched");

        const char* unknown[] = {"JBroGameHost.exe", "--fullscreen"};
        parsed = JBro::ParseGameHostArguments(2, unknown);
        Check(false == parsed.IsValid() && parsed.error.find("--fullscreen") != JBro::String::npos, "an unknown key is an error");

        const char* dangling[] = {"JBroGameHost.exe", "--project"};
        parsed = JBro::ParseGameHostArguments(2, dangling);
        Check(false == parsed.IsValid() && parsed.error.find("--project") != JBro::String::npos, "a key without a value is an error");

        const char* empty[] = {"JBroGameHost.exe", "--canvas", ""};
        parsed = JBro::ParseGameHostArguments(3, empty);
        Check(false == parsed.IsValid(), "an empty value is an error");

        Check(JBro::ParseGameHostArguments(0, nullptr).IsValid(), "no argument array at all is the empty project");
    }

    // **처음 읽을 캔버스.** `--canvas` 가 이기고, 없으면 프로젝트의 `StartupCanvas`, 상대경로는 프로젝트 폴더 기준이다.
    void TestTheStartupCanvasComesFromTheArgumentsOrTheProject()
    {
        JBro::GameHostArguments arguments;
        JBro::ProjectFile project;
        Check(JBro::ResolveStartupCanvasPath(arguments, project, "C:/p/game.jproject").empty(), "nothing set is nothing");

        project.build.startupCanvas = "Scenes/start.jcanvas";
        Check(JBro::ResolveStartupCanvasPath(arguments, project, "C:/p/game.jproject") == "C:/p/Scenes/start.jcanvas",
            "the project's startup canvas resolves against the project folder");

        arguments.canvasFile = "other.jcanvas";
        Check(JBro::ResolveStartupCanvasPath(arguments, project, "C:/p/game.jproject") == "C:/p/other.jcanvas",
            "--canvas wins and resolves the same way");

        arguments.canvasFile = "D:/abs/x.jcanvas";
        Check(JBro::ResolveStartupCanvasPath(arguments, project, "C:/p/game.jproject") == "D:/abs/x.jcanvas",
            "an absolute path stays as it is");
    }
}

namespace
{
    // **실행 파일 옆의 프로젝트**(D-232). 게임 빌드가 내놓은 폴더는 인자 없이 띄워도 제 프로젝트를 연다. 여럿이면 이름 차례로 첫 것이고,
    // 아래 폴더의 것은 보지 않는다. 패키지로 연 프로젝트의 시작 캔버스는 에셋 폴더 기준 경로 그대로다.
    void TestTheProjectBesideTheExecutableIsFound()
    {
        namespace fs = std::filesystem;
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const JBro::String folderText = platform.GetExecutableFolder();
        const fs::path folder(std::u8string(reinterpret_cast<const char8_t*>(folderText.c_str()), folderText.size()));
        Check(false == folderText.empty() && fs::is_directory(folder), "the test binary has a folder");
        // NTFS 는 대소문자를 가리지 않고 `alpha` 를 먼저 열거한다. 바이트 차례로는 `Zeta` 가 먼저다 - 열거 차례를 믿지 않는지 본다.
        const fs::path zeta = folder / "Zeta.jproject";
        const fs::path alpha = folder / "alpha.jproject";
        const fs::path nested = folder / "JBroBesideProbe" / "Aardvark.jproject";
        std::error_code ignored;
        fs::remove(zeta, ignored);
        fs::remove(alpha, ignored);
        // 옆에 아무 프로젝트도 없어야 이 시험이 뜻이 있다(빌드 폴더에는 없다).
        const JBro::String before = JBro::FindProjectBesideExecutable(platform);
        Check(before.empty(), "a folder with no project gives nothing");
        fs::create_directories(nested.parent_path(), ignored);
        std::ofstream(nested) << "Version: 1\n";
        std::ofstream(zeta) << "Version: 1\n";
        std::ofstream(alpha) << "Version: 1\n";
        const JBro::String found = JBro::FindProjectBesideExecutable(platform);
        fs::remove(zeta, ignored);
        fs::remove(alpha, ignored);
        fs::remove_all(nested.parent_path(), ignored);
        Check(found.size() > 14 && found.compare(found.size() - 14, 14, "/Zeta.jproject") == 0,
            "the first project by byte order beside the executable is found, not one in a subfolder");

        JBro::ProjectFile project;
        project.build.startupCanvas = "Canvases/Main.jcanvas";
        JBro::GameHostArguments arguments;
        Check(JBro::ResolvePackagedStartupCanvas(arguments, project) == "Canvases/Main.jcanvas", "a packaged game starts on its build canvas");
        arguments.canvasFile = "Canvases/Other.jcanvas";
        Check(JBro::ResolvePackagedStartupCanvas(arguments, project) == "Canvases/Other.jcanvas", "and --canvas overrides it");
        platform.Shutdown();
    }
}

int RunGameHostArgumentTests()
{
    TestArgumentsAreParsedStrictly();
    TestTheStartupCanvasComesFromTheArgumentsOrTheProject();
    TestTheProjectBesideTheExecutableIsFound();
    std::cout << "Game host argument tests passed.\n";
    return 0;
}
