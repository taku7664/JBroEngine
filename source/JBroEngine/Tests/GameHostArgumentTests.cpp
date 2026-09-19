#include <JBro/Host/GameHostArguments.h>
#include <JBro/Host/ProjectFile.h>

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

int RunGameHostArgumentTests()
{
    TestArgumentsAreParsedStrictly();
    TestTheStartupCanvasComesFromTheArgumentsOrTheProject();
    std::cout << "Game host argument tests passed.\n";
    return 0;
}
