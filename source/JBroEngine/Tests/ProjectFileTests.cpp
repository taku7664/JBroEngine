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

    // 이 엔진이 요구하는 두 키다(D-99). 이것이 없는 파일은 거절된다.
    const char* const RequiredKeys =
        "EngineVersion: 0.1.0\n"
        "Framework: 2D\n";

    // 기존 엔진이 실제로 쓰던 파일의 모양이다. 키 이름과 중첩까지 그대로다 —
    // 확장자를 같이 쓰는 이상 그 모양을 계속 읽는다. **다만 이대로는 열리지 않는다**(D-99).
    // 위의 두 키가 없기 때문이고, 그것을 재는 음성 테스트가 아래에 있다.
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
        "AssetDirectory: Contents/Art\n"
        "AssetIgnorePatterns:\n"
        "  - *.tmp\n"
        "  - \"~$*\"\n"
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
        JBro::String text(RequiredKeys);
        text.append(LegacyProject);
        // 인자 평가 순서는 정해져 있지 않다. 먼저 돌리고 나서 물어본다.
        const bool parsed = Parse(text.c_str(), project, error);
        if (false == parsed)
        {
            std::cout << "  project parse failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
        }
        Check(parsed, "the legacy project shape must parse");

        Check(project.version == 1, "the version must come through");
        Check(project.engineVersion == "0.1.0", "the engine version must come through");
        Check(project.framework == JBro::FrameworkKind::Framework2D,
            "and the framework it runs on");
        Check(project.resolutionWidth == 600 && project.resolutionHeight == 800,
            "the resolution must come through");
        Check(project.pixelsPerUnit == 100.0f, "pixels per unit must come through");
        Check(project.debugModeEnabled == false, "a false flag must stay false");
        Check(project.scriptSourceDirectory == "Contents", "the script source directory must come through");
        Check(project.assetDirectory == "Contents/Art", "the asset directory must come through");
        Check(project.assetIgnorePatterns.Size() == 2
            && project.assetIgnorePatterns[0] == "*.tmp"
            && project.assetIgnorePatterns[1] == "~$*",
            "the ignore patterns must come through as a top-level sequence");
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

    void TestRequiresTheEngineVersionAndTheFramework()
    {
        JBro::ProjectFile project;
        JBro::ProjectFileError error;

        // 기존 엔진이 쓰던 파일이 이 모양이다. 더 이상 열리지 않는다(D-99) -
        // 어느 엔진으로 어느 차원을 여는지 모른 채 열면 런처가 고를 수가 없고,
        // 3D 프로젝트가 조용히 2D 로 열린다.
        Check(false == Parse(LegacyProject, project, error),
            "a project without the engine version and the framework must be refused");

        Check(false == Parse("Version: 1\nFramework: 2D\n", project, error),
            "the engine version alone may not be left out");
        Check(false == Parse("Version: 1\nEngineVersion: 0.1.0\n", project, error),
            "and neither may the framework");
        Check(false == Parse("Version: 1\nEngineVersion: \"\"\nFramework: 2D\n", project, error),
            "an empty engine version is the same as not saying which engine");
        Check(false == Parse("Version: 1\nEngineVersion: 0.1.0\nFramework: 4D\n", project, error),
            "the framework must be 2D or 3D and nothing else");
        Check(error.line == 3, "and the refusal must name the line that said it");

        Check(Parse("Version: 1\nEngineVersion: 0.1.0\nFramework: 3D\n", project, error),
            "a 3D project must parse");
        Check(project.framework == JBro::FrameworkKind::Framework3D,
            "and must come back as 3D rather than the default");
        Check(Parse("Version: 1\nEngineVersion: 0.1.0\nFramework: 2d\n", project, error),
            "the framework is written by hand, so case must not decide it");
        Check(project.framework == JBro::FrameworkKind::Framework2D, "lowercase 2d is 2D");
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
        Check(Parse("Version: 2\nEngineVersion: 0.1.0\nFramework: 2D\n", project, error),
            "a project with only the keys it must have should parse");
        Check(project.version == 2, "the version must come through");
        Check(project.resolutionWidth == 1920 && project.resolutionHeight == 1080,
            "keys the file omits must keep their defaults");
        Check(project.build.scriptOutputLibraryPath == "GameScript.dll",
            "nested defaults must survive too");
        Check(project.assetDirectory == "Contents/Assets" && project.assetIgnorePatterns.IsEmpty(),
            "the asset directory defaults to Contents/Assets with nothing ignored (D-111)");
    }

    // 손으로 옮겨 적은 모양이 아니라 기존 엔진이 실제로 저장한 파일을 읽는다.
    // **이제는 거절되는 것이 맞다**(D-99) - 그 파일에는 `EngineVersion` 도 `Framework` 도
    // 없다. 옮겨 올 프로젝트가 없어서 내린 결정이고, 실제 파일로 그 사실을 확인한다.
    // 파일이 없으면 건너뛰되 조용히 지나가지 않는다 — 이 기계에만 있는 파일이다.
    void TestRefusesARealLegacyProjectFileIfPresent()
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
        if (JBro::LoadProjectFile(path.c_str(), project, error))
        {
            Check(false, "a real legacy project file must be refused now that two keys are required");
        }
        if (error.message == "cannot open the project file")
        {
            std::cout << "  [skip] no legacy project on this machine" << std::endl;
            return;
        }
        // 거절은 맞되 **이유가 맞아야 한다.** 형식이 틀려서 거절된 것이라면 그 파일을
        // 읽는 길이 어딘가에서 깨진 것이고, 그것은 이 결정과 아무 상관이 없다.
        Check(error.message.find("EngineVersion") != JBro::String::npos,
            "and must be refused for the key it does not have, not for something else");
    }

    void TestAnEmptyStringIsAValueNotABlock()
    {
        // `Key: ""` 는 "비었다는 값" 이고 `Key:` 는 "아래에 블록이 온다" 다.
        // 따옴표를 먼저 벗기면 둘이 같아져서, 명시적으로 비운 키가 통째로 무시되고
        // 뒤따르는 줄까지 그 블록의 내용으로 건너뛰어진다.
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        const char* text =
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "LastOpenedCanvasPath: Scenes/Opening.jcanvas\n";
        if (false == JBro::ParseProjectFile(text, std::strlen(text), project, error))
        {
            std::cout << "  parse failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
            Check(false, "a project with an explicitly empty value must parse");
        }
        Check(project.scriptOutputLibraryPath.empty(),
            "an empty string must replace the default, not be skipped");
        Check(project.lastOpenedCanvasPath == "Scenes/Opening.jcanvas",
            "the key after it must not be swallowed as part of a block");

        // 값이 없는 키는 여전히 블록의 시작이다.
        JBro::ProjectFile withBlock;
        const char* blockText =
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "Build:\n"
            "  ProductName: Game\n";
        Check(JBro::ParseProjectFile(blockText, std::strlen(blockText), withBlock, error),
            "a key with no value must still open a block");
        Check(withBlock.build.productName == "Game", "and its contents must be read");
    }

}

int RunProjectFileTests()
{
    TestAnEmptyStringIsAValueNotABlock();
    TestReadsTheLegacyProjectShape();
    TestRefusesARealLegacyProjectFileIfPresent();
    TestRefusesWhatItDoesNotUnderstand();
    TestRequiresTheEngineVersionAndTheFramework();
    TestScriptModulePathResolution();
    TestDefaultsSurviveAnEmptyProject();
    std::cout << "Project file tests passed.\n";
    return 0;
}
