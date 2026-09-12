#include <JBro/Host/ProjectFile.h>

#include <cstdlib>
#include <cstring>
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

    bool Parse(const char* text, JBro::ProjectFile& result, JBro::ProjectFileError& error)
    {
        return JBro::ParseProjectFile(text, std::strlen(text), result, error);
    }

    // 기존 엔진이 실제로 쓰는 파일의 모양이다. 키 이름과 중첩까지 그대로다 —
    // 그 쪽 프로젝트를 열 수 있어야 확장자를 같이 쓰는 의미가 있다.
    const char* const LegacyProject =
        "Version: 1\n"
        "RootPath: .\n"
        "ResolutionWidth: 600\n"
        "ResolutionHeight: 800\n"
        "CanvasViewCamX: 2.0111556\n"
        "PixelsPerUnit: 100\n"
        "DefaultFontFamilyGuid: \"\"\n"
        "FallbackFontFamilies:\n"
        "  []\n"
        "DebugModeEnabled: false\n"
        "EditorLocale: ko-KR\n"
        "ScriptSourceDirectory: Contents\n"
        "ScriptBuildCommand: \"\"\n"
        "ScriptOutputLibraryPath: x64/Debug/GameScript.dll\n"
        "ScriptAutoRebuildEnabled: true\n"
        "Build:\n"
        "  ProductName: Test\n"
        "  EnableWindows: true\n"
        "  EnableWeb: true\n"
        "  EnableAndroid: false\n"
        "  OutputDirectory: C:/Users/x/Build Test\n"
        "  StartupCanvas: LayerBlendTest.jcanvas\n"
        "  BuildCanvases:\n"
        "    - LayerBlendTest.jcanvas\n"
        "    - Scenes/Tetris.jcanvas\n"
        "  AlwaysIncludeAssets:\n"
        "    []\n"
        "  ScriptOutputLibraryPath: GameScript.dll\n"
        "LastOpenedCanvasPath: NewScene.jcanvas\n"
        "AssetWatchIgnorePatterns:\n"
        "  - \"*.tmp\"\n"
        "  - ~$*\n"
        "InputLayers:\n"
        "  - Modal\n"
        "  - UI\n";

    void TestReadsTheLegacyProjectShape()
    {
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        // 인자 평가 순서는 정해져 있지 않다. 먼저 돌리고 나서 물어본다.
        const bool parsed = Parse(LegacyProject, project, error);
        if (false == parsed)
        {
            std::cout << "  project parse failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
        }
        Check(parsed, "the legacy project shape must parse");

        Check(project.version == 1, "the version must come through");
        Check(project.resolutionWidth == 600 && project.resolutionHeight == 800,
            "the resolution must come through");
        Check(project.pixelsPerUnit == 100.0f, "pixels per unit must come through");
        Check(project.debugModeEnabled == false, "a false flag must stay false");
        Check(project.scriptSourceDirectory == "Contents", "the script source directory must come through");
        Check(project.scriptOutputLibraryPath == "x64/Debug/GameScript.dll",
            "the editor script library path must come through");
        Check(project.lastOpenedCanvasPath == "NewScene.jcanvas",
            "a key after the nested map must not be swallowed by it");

        Check(project.build.productName == "Test", "the nested product name must come through");
        Check(project.build.enableWindows && project.build.enableWeb
            && false == project.build.enableAndroid,
            "the nested flags must come through independently");
        Check(project.build.outputDirectory == "C:/Users/x/Build Test",
            "a value with spaces must survive");
        Check(project.build.startupCanvas == "LayerBlendTest.jcanvas",
            "the startup canvas must come through");
        Check(project.build.scriptOutputLibraryPath == "GameScript.dll",
            "the nested script path must not be confused with the top-level one");
        Check(project.build.buildCanvases.Size() == 2
            && project.build.buildCanvases[0] == "LayerBlendTest.jcanvas"
            && project.build.buildCanvases[1] == "Scenes/Tetris.jcanvas",
            "a nested sequence must keep its items in order");
    }

    void TestRefusesWhatItDoesNotUnderstand()
    {
        JBro::ProjectFile project;
        JBro::ProjectFileError error;

        Check(false == Parse("Version: 1\nResolutionWidth: wide\n", project, error),
            "a number key with a non-number value must be refused");
        Check(error.line == 2, "the error must name the line it failed on");

        Check(false == Parse("Version: 1\nDebugModeEnabled: yes\n", project, error),
            "a boolean key only accepts true and false");

        Check(false == Parse("Version: 1\nRootPath\n", project, error),
            "a line without a colon must be refused");

        Check(false == Parse("Version: 1\n\tRootPath: .\n", project, error),
            "tab indentation must be refused rather than guessed at");

        Check(false == Parse("Version: 1\n   RootPath: .\n", project, error),
            "an odd indent must be refused");

        Check(false == Parse("Version: 1\nProductName: \"unterminated\n", project, error),
            "an unterminated quote must be refused");

        Check(false == Parse("- orphan\n", project, error),
            "a sequence item with no key above it must be refused");

        Check(false == Parse("Version: 0\n", project, error),
            "a zero version must be refused");
    }

    void TestScriptModulePathResolution()
    {
        JBro::ProjectFile project;
        project.scriptOutputLibraryPath = "x64/Debug/GameScript.dll";
        const JBro::String resolved =
            JBro::ResolveScriptModulePath(project, "C:/games/Test/Test.jproject");
        Check(resolved == "C:/games/Test/x64/Debug/GameScript.dll",
            "a relative script path must resolve next to the project file");

        project.scriptOutputLibraryPath = "D:/prebuilt/GameScript.dll";
        Check(JBro::ResolveScriptModulePath(project, "C:/games/Test/Test.jproject")
            == "D:/prebuilt/GameScript.dll",
            "an absolute script path must be left alone");

        project.scriptOutputLibraryPath.clear();
        Check(JBro::ResolveScriptModulePath(project, "C:/games/Test/Test.jproject").empty(),
            "a project with no script path must resolve to nothing");
    }

    void TestDefaultsSurviveAnEmptyProject()
    {
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(Parse("Version: 2\n", project, error), "a project with only a version must parse");
        Check(project.version == 2, "the version must come through");
        Check(project.resolutionWidth == 1920 && project.resolutionHeight == 1080,
            "keys the file omits must keep their defaults");
        Check(project.build.scriptOutputLibraryPath == "GameScript.dll",
            "nested defaults must survive too");
    }

    // 손으로 옮겨 적은 모양이 아니라 기존 엔진이 실제로 저장한 파일을 읽는다.
    // 없으면 건너뛰되 조용히 지나가지 않는다 — 이 기계에만 있는 파일이다.
    void TestReadsARealLegacyProjectFileIfPresent()
    {
        // 경로를 리터럴로 박지 않는다. 이 기계의 사용자 폴더 이름에 한글이 들어 있다.
        char* profile = nullptr;
        std::size_t profileLength = 0;
        if (_dupenv_s(&profile, &profileLength, "USERPROFILE") != 0 || profile == nullptr)
        {
            std::cout << "  [skip] no USERPROFILE; legacy project not read" << std::endl;
            return;
        }
        JBro::String path(profile);
        std::free(profile);
        path.append("/source/repos/JBroEngine/TestProject/Test/Test.jproject");
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        if (false == JBro::LoadProjectFile(path.c_str(), project, error))
        {
            if (error.line == 0 && error.message == "cannot open the project file")
            {
                std::cout << "  [skip] no legacy project on this machine" << std::endl;
                return;
            }
            std::cout << "  legacy project failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
            Check(false, "a real legacy project file must parse");
        }
        Check(project.version >= 1, "a real legacy project must carry a version");
        Check(false == project.scriptOutputLibraryPath.empty(),
            "a real legacy project must name its script library");
    }
}

int RunProjectFileTests()
{
    TestReadsTheLegacyProjectShape();
    TestReadsARealLegacyProjectFileIfPresent();
    TestRefusesWhatItDoesNotUnderstand();
    TestScriptModulePathResolution();
    TestDefaultsSurviveAnEmptyProject();
    std::cout << "Project file tests passed.\n";
    return 0;
}
