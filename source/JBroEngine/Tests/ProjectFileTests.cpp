#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
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
        "TextureFilter: Linear\n"
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
        // `PixelsPerUnit` 은 D-117 로 프로젝트에서 빠졌다. 옛 파일에 남은 키는 모르는 키로 건너뛴다.
        Check(project.textureFilter == JBro::TextureFilter::Linear, "the texture filter must come through");
        // 프로젝트 기본에 `Default` 나 모르는 이름은 없다.
        JBro::ProjectFile refused;
        JBro::String bad(RequiredKeys);
        bad.append("TextureFilter: Default\n");
        Check(false == Parse(bad.c_str(), refused, error), "Default is not a project texture filter");
        JBro::String none(RequiredKeys);
        Check(Parse(none.c_str(), refused, error) && refused.textureFilter == JBro::TextureFilter::Nearest,
            "a project that says nothing samples nearest");
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
    // 이 기계의 사용자 폴더 이름에 한글이 들어 있다. 환경 변수는 와이드로 받아 UTF-8 로 바꾼다 - 플랫폼의 경로는
    // UTF-8 이고(D-112), 좁은 `USERPROFILE` 은 ANSI 라 그대로 넘기면 없는 파일이 된다.
    bool UserProfileUtf8(JBro::String& out)
    {
        wchar_t* profile = nullptr;
        std::size_t length = 0;
        if (_wdupenv_s(&profile, &length, L"USERPROFILE") != 0 || profile == nullptr)
        {
            return false;
        }
        const std::u8string text = std::filesystem::path(profile).generic_u8string();
        std::free(profile);
        out = JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
        return true;
    }

    void TestRefusesARealLegacyProjectFileIfPresent()
    {
        // 경로를 리터럴로 박지 않는다.
        JBro::String path;
        if (false == UserProfileUtf8(path))
        {
            std::cout << "  [skip] no USERPROFILE; legacy project not read" << std::endl;
            return;
        }
        path.append("/source/repos/JBroEngine/TestProject/Test/Test.jproject");
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        if (JBro::LoadProjectFile(platform, path.c_str(), project, error))
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


    // **고쳐 쓰기는 원문을 타고 간다**(D-137). 우리가 모르는 키도, 주석도, 시퀀스도
    // 그 자리에 남아야 한다 - 통째로 다시 쓰면 남의 설정이 조용히 사라진다.
    void TestRewritingKeepsWhatItDoesNotKnow()
    {
        const char* text =
            "# 이 줄은 주석이다\n"
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "ResolutionWidth: 1920\n"
            "ResolutionHeight: 1080\n"
            "SomeFutureKey: keep me\n"
            "AssetIgnorePatterns:\n"
            "  - *.psd\n"
            "Build:\n"
            "  ProductName: Game\n"
            "  UnknownBuildKey: keep me too\n";

        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error),
            "the probe project must parse");
        Check(project.resolutionWidth == 1920, "and read what it knows");

        project.resolutionWidth = 1280;
        project.resolutionHeight = 720;
        project.build.productName = "Renamed";
        // 원문에 없던 키다. 뒤에 더해져야 한다.
        project.assetDirectory = "Art/Assets";

        JBro::String written;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error),
            "rewriting must go through");

        Check(written.find("# 이 줄은 주석이다") != JBro::String::npos, "comments stay");
        Check(written.find("SomeFutureKey: keep me") != JBro::String::npos,
            "a key we do not know stays, with its value");
        Check(written.find("UnknownBuildKey: keep me too") != JBro::String::npos,
            "and so does one inside a block");
        Check(written.find("  - *.psd") != JBro::String::npos, "sequences are left alone");
        Check(written.find("ResolutionWidth: 1280") != JBro::String::npos,
            "the value we changed is the one that changed");
        Check(written.find("1920") == JBro::String::npos, "and the old one is gone");
        Check(written.find("ProductName: Renamed") != JBro::String::npos,
            "inside the block too");
        Check(written.find("AssetDirectory: Art/Assets") != JBro::String::npos,
            "a key the file did not have is added");

        // 다시 읽으면 같은 값이 나와야 한다. 쓴 글자가 읽히지 않으면 그 프로젝트는 열리지 않는다.
        JBro::ProjectFile again;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), again, error),
            "what was written must read back");
        Check(again.resolutionWidth == 1280 && again.resolutionHeight == 720,
            "with the values that were set");
        Check(again.build.productName == "Renamed", "and the block's too");
        Check(again.assetDirectory == "Art/Assets", "and the added one");
        Check(again.engineVersion == "1.0.0" && again.framework == JBro::FrameworkKind::Framework2D,
            "and what was not touched is untouched");
    }

    // **새 프로젝트를 세운다**(D-160, 기존 `CProjectManager::CreateProject`). 폴더·프로젝트 파일·에셋 폴더가
    // 서고 그대로 열린다. 이미 있는 폴더 위에는 세우지 않고, 파일 이름이 될 수 없는 이름은 거절한다.
    void TestCreatesANewProject()
    {
        namespace fs = std::filesystem;
        std::error_code ignored;
        const fs::path parent = fs::temp_directory_path() / "JBroCreateProjectProbe";
        fs::remove_all(parent, ignored);
        fs::create_directories(parent, ignored);
        const std::u8string parentText = parent.u8string();
        const JBro::String parentUtf8(reinterpret_cast<const char*>(parentText.c_str()), parentText.size());

        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");

        // 한글과 공백이 섞인 이름이다. 폴더와 파일 이름이 UTF-8 로 끝까지 가야 한다.
        const char* name = "\xEC\x83\x88 \xEA\xB2\x8C\xEC\x9E\x84";  // "새 게임"
        JBro::String path;
        JBro::ProjectFileError error;
        if (false == JBro::CreateProjectFile(platform, parentUtf8.c_str(), name,
                JBro::FrameworkKind::Framework3D, "0.1.0", path, error))
        {
            std::cout << "  create failed: " << error.message.c_str() << std::endl;
            Check(false, "a new project must be created in an empty folder");
        }
        const fs::path root = parent / fs::path(std::u8string(u8"새 게임"));
        Check(fs::is_regular_file(root / std::u8string(u8"새 게임.jproject"), ignored),
            "the project file stands in a folder of the same name");
        Check(fs::is_directory(root / "Contents" / "Assets", ignored), "with the asset folder");

        JBro::ProjectFile opened;
        Check(JBro::LoadProjectFile(platform, path.c_str(), opened, error), "and it opens");
        Check(opened.framework == JBro::FrameworkKind::Framework3D, "as the framework that was chosen");
        Check(opened.engineVersion == "0.1.0", "with the engine version that made it");
        Check(opened.build.productName == name, "and the name as the product name");
        {
            // 새 파일은 첫 줄부터 키다. 빈 원문을 빈 줄 하나로 세어 파일이 빈 줄로 시작했다.
            JBro::Array<std::byte> bytes;
            Check(platform.ReadWholeFile(path.c_str(), bytes) && bytes.Size() > 0
                    && static_cast<char>(bytes[0]) == 'V',
                "the new file starts with its first key, not a blank line");
        }

        // **있는 폴더 위에는 세우지 않는다.** 남의 파일을 덮어 프로젝트를 만들면 되돌릴 길이 없다.
        const JBro::String firstPath = path;
        Check(false == JBro::CreateProjectFile(platform, parentUtf8.c_str(), name,
                JBro::FrameworkKind::Framework2D, "0.1.0", path, error),
            "the same name again is refused");
        Check(error.createFailure == JBro::ProjectCreateFailure::AlreadyExists, "and says it is already there");
        JBro::ProjectFile still;
        Check(JBro::LoadProjectFile(platform, firstPath.c_str(), still, error)
                && still.framework == JBro::FrameworkKind::Framework3D,
            "and the first project is untouched");

        for (const char* bad : {"", " lead", "trail ", "a/b", "a\\b", "what?", "..", "dot."})
        {
            Check(false == JBro::CreateProjectFile(platform, parentUtf8.c_str(), bad,
                    JBro::FrameworkKind::Framework2D, "0.1.0", path, error),
                "a name that cannot be a file name is refused");
        }
        Check(false == JBro::CreateProjectFile(platform, parentUtf8.c_str(), "NoVersion",
                JBro::FrameworkKind::Framework2D, "", path, error),
            "a project without an engine version is refused");
        Check(false == fs::exists(parent / "NoVersion", ignored), "and leaves nothing behind");

        fs::remove_all(parent, ignored);
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
    TestRewritingKeepsWhatItDoesNotKnow();
    TestCreatesANewProject();
    std::cout << "Project file tests passed.\n";
    return 0;
}
