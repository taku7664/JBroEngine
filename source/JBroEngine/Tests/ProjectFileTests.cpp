#include <JBro/Framework2DSystem/PhysicsThreads.h>
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


    // **이미 겹쳐 있는 파일은 저장하면서 고쳐진다**(D-189). 불어나기를 멈추는 것만으로는
    // 이미 불어난 파일이 그대로 남고, 읽을 때 마지막 줄이 앞의 줄을 조용히 덮는다.
    void TestSavingCollapsesKeysThatWereWrittenTwice()
    {
        const char* text =
            "Version: 1\n"
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "SomeFutureKey: keep me\n"
            "LastOpenedCanvasPath: \n"
            "AssetIgnorePatterns:\n"
            "  - \"*.psd\"\n"
            "Build:\n"
            "  ProductName: Hero\n"
            "  ProductName: Hero\n"
            "LastOpenedCanvasPath: \n"
            "AssetIgnorePatterns:\n"
            "  - \"*.psd\"\n";

        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error),
            "the damaged project must still parse");

        JBro::String once;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), once, error),
            "the rewrite must go through");

        const auto countOf = [](const JBro::String& text, const char* needle) {
            std::size_t found = 0;
            std::size_t at = text.find(needle);
            while (at != JBro::String::npos)
            {
                ++found;
                at = text.find(needle, at + 1);
            }
            return found;
        };
        Check(countOf(once, "LastOpenedCanvasPath") == 1,
            "a key written twice must come back once");
        Check(countOf(once, "ProductName") == 1, "inside the block too");
        Check(countOf(once, "AssetIgnorePatterns") == 1, "and so must the sequence");
        Check(countOf(once, "*.psd") == 1, "with its items, not two copies of them");
        Check(once.find("SomeFutureKey: keep me") != JBro::String::npos,
            "a key the engine does not know must still survive");

        // 고친 뒤로는 가만히 있어야 한다.
        JBro::String twice;
        Check(JBro::WriteProjectFileText(project, once.c_str(), once.size(), twice, error),
            "the second rewrite must go through");
        Check(once == twice, "and the repaired file must not move again");
    }

    // **두 번 저장해도 파일이 불어나지 않는다**(D-189). 값이 빈 키(`ProductName: `)를
    // "적지 않은 키" 로 세는 바람에, 저장할 때마다 같은 키가 뒤에 하나씩 더 붙었다 -
    // 실제 프로젝트 파일이 그렇게 망가져 있었다.
    void TestSavingTwiceDoesNotGrowTheFile()
    {
        const char* text =
            "Version: 1\n"
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "LastOpenedCanvasPath: \n"
            "Build:\n"
            "  ProductName: \n"
            "  StartupCanvas: \n";

        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error),
            "the probe project must parse");

        JBro::String once;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), once, error),
            "the first rewrite must go through");
        JBro::String twice;
        Check(JBro::WriteProjectFileText(project, once.c_str(), once.size(), twice, error),
            "the second rewrite must go through");
        // 첫 저장은 원문에 없던 키를 채우므로 늘어나는 것이 맞다. **그 뒤로 가만히 있어야** 한다.
        JBro::String thrice;
        Check(JBro::WriteProjectFileText(project, twice.c_str(), twice.size(), thrice, error),
            "the third rewrite must go through");
        Check(twice == thrice, "saving a file that did not change must leave it byte for byte");
        Check(once == twice, "and the pass that only fills in missing keys must settle at once");

        const auto countOf = [](const JBro::String& text, const char* needle) {
            std::size_t found = 0;
            std::size_t at = text.find(needle);
            while (at != JBro::String::npos)
            {
                ++found;
                at = text.find(needle, at + 1);
            }
            return found;
        };
        Check(countOf(twice, "LastOpenedCanvasPath") == 1,
            "an empty top-level key must appear once, not twice");
        Check(countOf(twice, "ProductName") == 1,
            "and an empty key inside a block must too");
        Check(countOf(twice, "StartupCanvas") == 1, "all of them");
    }

    // **무시 패턴을 고치면 파일에 간다**(D-189, 기존 프로젝트 설정의 에셋 감시 칸).
    // 시퀀스를 지나치던 동안에는 설정 화면에서 고칠 길도, 고쳐도 남을 길도 없었다.
    void TestRewritingTheIgnorePatterns()
    {
        const char* text =
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "AssetIgnorePatterns:\n"
            "  - \"*.psd\"\n"
            "  - \"~$*\"\n"
            "SomeFutureKey: keep me\n";

        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error),
            "the probe project must parse");
        Check(project.assetIgnorePatterns.Size() == 2, "and read both patterns");

        project.assetIgnorePatterns.Clear();
        project.assetIgnorePatterns.Add(JBro::String("*.tmp"));
        JBro::String written;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error),
            "rewriting must go through");
        Check(written.find("*.tmp") != JBro::String::npos, "the new pattern must be written");
        Check(written.find("*.psd") == JBro::String::npos,
            "and the ones that were taken out must be gone");
        Check(written.find("SomeFutureKey: keep me") != JBro::String::npos,
            "while the key after the sequence stays where it was");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error),
            "the rewritten text must parse");
        Check(reread.assetIgnorePatterns.Size() == 1
                && reread.assetIgnorePatterns[0] == "*.tmp",
            "and read back as the one pattern that is left");

        // 비우면 `[]` 다. 머리줄만 남기면 다음 읽기가 값 없는 맵으로 본다.
        project.assetIgnorePatterns.Clear();
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error),
            "rewriting an empty list must go through");
        Check(written.find("AssetIgnorePatterns: []") != JBro::String::npos,
            "an empty list is written as an empty sequence");
        JBro::ProjectFile emptied;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), emptied, error),
            "and that must parse");
        Check(emptied.assetIgnorePatterns.IsEmpty(), "with nothing in it");

        // 원문에 그 키가 없으면 **패턴이 있을 때만** 붙인다.
        const char* bare = "EngineVersion: 1.0.0\nFramework: 2D\n";
        JBro::ProjectFile none;
        Check(JBro::ParseProjectFile(bare, std::strlen(bare), none, error), "the bare file parses");
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error),
            "rewriting the bare file must go through");
        Check(written.find("AssetIgnorePatterns") == JBro::String::npos,
            "a file with no patterns must not grow an empty list just by being saved");
    }

    // **고쳐 쓰기는 원문을 타고 간다**(D-137). 우리가 모르는 키도, 주석도 그 자리에
    // 남아야 한다 - 통째로 다시 쓰면 남의 설정이 조용히 사라진다.
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
        // **무시 패턴은 이제 고쳐 쓴다**(D-189). 그전에는 시퀀스를 통째로 지나쳐서,
        // 설정 화면에서 고쳐도 파일에 가지 않았다. 따옴표는 늘 붙인다 - `~$*` 처럼
        // YAML 이 다르게 읽는 글자로 시작하는 패턴이 있다.
        Check(written.find("  - \"*.psd\"") != JBro::String::npos,
            "the ignore patterns are written back");
        {
            JBro::ProjectFile reread;
            Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error),
                "the rewritten text must parse again");
            Check(reread.assetIgnorePatterns.Size() == 1
                    && reread.assetIgnorePatterns[0] == "*.psd",
                "and the pattern must survive the round trip unchanged");
        }
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

    // **고른 경로를 프로젝트 기준으로 적는다**(D-164). 절대경로로 적으면 프로젝트를 옮기는 순간 깨진다.
    void TestPathsAreMadeRelativeToTheProject()
    {
        const char* project = "C:\\Games\\Space\\Space.jproject";
        Check(JBro::MakeProjectRelativePath("C:\\Games\\Space\\Contents\\Assets", project) == "Contents/Assets",
            "a folder inside the project becomes relative with forward slashes");
        Check(JBro::MakeProjectRelativePath("c:/games/space/Build", project) == "Build",
            "case and separators do not matter on Windows");
        Check(JBro::MakeProjectRelativePath("C:\\Games\\Space", project) == ".",
            "the project folder itself is '.'");
        Check(JBro::MakeProjectRelativePath("C:\\Games\\SpaceAssets\\x", project) == "C:\\Games\\SpaceAssets\\x",
            "a sibling whose name starts the same is outside");
        Check(JBro::MakeProjectRelativePath("D:\\Elsewhere", project) == "D:\\Elsewhere",
            "a path outside the project stays as it was");
        Check(JBro::MakeFolderRelativePath("C:\\Games\\Space\\Contents\\Assets\\Levels\\One.jcanvas",
                  "C:\\Games\\Space\\Contents\\Assets\\") == "Levels/One.jcanvas",
            "canvas paths are relative to the asset folder, trailing slash or not");
    }

    // **새 프로젝트를 세운다**(D-160, 기존 `CProjectManager::CreateProject`). 폴더·프로젝트 파일·에셋 폴더가
    // 서고 그대로 열린다. 이미 있는 폴더 위에는 세우지 않고, 파일 이름이 될 수 없는 이름은 거절한다.
    // **프로젝트 폰트 목록**(D-200 (6), text-plan §5 의 3 단계). 순서가 있는 아이디의 시퀀스이고, 설정 창의 빈 줄(고르지 않은
    // 폰트)은 적지 않으며, 아이디가 아닌 항목은 파일 오류다.
    void TestTheProjectFontList()
    {
        const char* text =
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "Fonts:\n"
            "  - 0123456789abcdef0123456789abcdef\n"
            "  - fedcba9876543210fedcba9876543210\n"
            "SomeFutureKey: keep me\n";
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error), "the font list parses");
        JBro::AssetId first;
        JBro::AssetId second;
        Check(JBro::Uuid::Parse("0123456789abcdef0123456789abcdef", 32, first)
                && JBro::Uuid::Parse("fedcba9876543210fedcba9876543210", 32, second),
            "the probe ids parse");
        Check(project.fonts.Size() == 2 && project.fonts[0] == first && project.fonts[1] == second,
            "both fonts are read in their order");

        // 순서를 바꾸고 빈 줄을 하나 끼운다. 빈 줄은 적히지 않는다.
        project.fonts.Clear();
        project.fonts.Add(second);
        project.fonts.Add(JBro::AssetId{});
        project.fonts.Add(first);
        JBro::String written;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error), "the list rewrites");
        Check(written.find("SomeFutureKey: keep me") != JBro::String::npos, "the key after the list stays");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error), "the rewritten text parses");
        Check(reread.fonts.Size() == 2 && reread.fonts[0] == second && reread.fonts[1] == first,
            "the new order comes back and the unset row is left out");

        // 비우면 `[]` 다.
        project.fonts.Clear();
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error), "an empty list rewrites");
        Check(written.find("Fonts: []") != JBro::String::npos, "an empty list is an empty sequence");
        JBro::ProjectFile emptied;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), emptied, error) && emptied.fonts.IsEmpty(),
            "and reads back empty");

        // 키가 없던 파일에는 폰트가 있을 때만 붙는다.
        const char* bare = "EngineVersion: 1.0.0\nFramework: 2D\n";
        JBro::ProjectFile none;
        Check(JBro::ParseProjectFile(bare, std::strlen(bare), none, error), "the bare file parses");
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error)
                && written.find("Fonts") == JBro::String::npos,
            "a file with no fonts does not grow a Fonts key");
        none.fonts.Add(first);
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error)
                && written.find("Fonts:\n  - 0123456789abcdef0123456789abcdef\n") != JBro::String::npos,
            "a font added to a bare file is appended");

        // 아이디가 아닌 항목은 거절한다. 조용히 건너뛰면 기본 폰트가 소리 없이 바뀐다.
        const char* broken = "EngineVersion: 1.0.0\nFramework: 2D\nFonts:\n  - sans.otf\n";
        JBro::ProjectFile refused;
        Check(false == JBro::ParseProjectFile(broken, std::strlen(broken), refused, error),
            "a font entry that is not an asset id is refused");
    }

    // 게임 로케일(D-226): 목록·기본·폴백. 비어 있으면 적지 않는다 - 로컬라이징을 쓰지 않는 파일은 저장해도 그대로다.
    void TestTheLocaleSettings()
    {
        const char* text =
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "Locales:\n"
            "  - ko-KR\n"
            "  - en-US\n"
            "DefaultLocale: ko-KR\n"
            "FallbackLocale: en-US\n"
            "SomeFutureKey: keep me\n";
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error), "the locale settings parse");
        Check(project.locales.Size() == 2 && project.locales[0] == "ko-KR" && project.locales[1] == "en-US",
            "the locale list is read in its order");
        Check(project.defaultLocale == "ko-KR" && project.fallbackLocale == "en-US", "and the default and fallback");

        project.locales.Add("ja-JP");
        project.fallbackLocale = "ja-JP";
        JBro::String written;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error), "the locales rewrite");
        Check(written.find("SomeFutureKey: keep me") != JBro::String::npos, "the key after the list stays");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.locales.Size() == 3
                && reread.locales[2] == "ja-JP" && reread.fallbackLocale == "ja-JP" && reread.defaultLocale == "ko-KR",
            "the new list and fallback come back");
        Check(written.find("Locales:") == written.rfind("Locales:"), "the list is written once");

        project.locales.Clear();
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error)
                && written.find("Locales: []") != JBro::String::npos,
            "an emptied list is an empty sequence");
        JBro::ProjectFile emptied;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), emptied, error) && emptied.locales.IsEmpty(),
            "and reads back empty");

        // 키가 없던 파일은 로케일이 없으면 그대로이고, 있으면 붙는다.
        const char* bare = "EngineVersion: 1.0.0\nFramework: 2D\n";
        JBro::ProjectFile none;
        Check(JBro::ParseProjectFile(bare, std::strlen(bare), none, error), "the bare file parses");
        // 에디터 언어(`EditorLocale`)는 늘 적힌다. 게임 언어의 세 키만 본다.
        const auto hasGameLocaleKey = [](const JBro::String& file) {
            return file.find("Locales") != JBro::String::npos || file.find("DefaultLocale") != JBro::String::npos
                || file.find("FallbackLocale") != JBro::String::npos;
        };
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error) && false == hasGameLocaleKey(written),
            "a project with no locales grows no locale keys");
        none.locales.Add(JBro::String());
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error) && false == hasGameLocaleKey(written),
            "an unnamed row from the settings window is not written");
        none.locales.Add("ko-KR");
        none.defaultLocale = "ko-KR";
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error)
                && written.find("Locales:\n  - ko-KR\n") != JBro::String::npos
                && written.find("DefaultLocale: ko-KR\n") != JBro::String::npos
                && written.find("FallbackLocale") == JBro::String::npos,
            "set locales are appended and the empty fallback is not");
    }

    // **비운 값은 빈 채로 돌아온다**(D-232). `Key: ` 는 블록 머리로 읽혀 기본값이 되살았다 - 설정 창에서 스크립트 경로를 비워도 저장 뒤
    // 개발 경로(`x64/Debug/GameScript.dll`)가 돌아왔다. 원문에 비어 있던 줄은 그대로 둔다.
    void TestAnEmptiedValueStaysEmpty()
    {
        const char* text =
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "ScriptOutputLibraryPath: bin/Game.dll\n"
            "LastOpenedCanvasPath: \n"
            "Build:\n"
            "  ProductName: Probe\n";
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error), "the probe parses");
        JBro::String written;
        // 적히지 않은 아는 키는 첫 저장이 더한다. 비어 있던 줄은 그 자리 그대로이고, 두 번째 저장은 바이트 하나 바꾸지 않는다.
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error)
                && written.find("\nLastOpenedCanvasPath: \n") != JBro::String::npos,
            "an untouched empty line keeps its bytes");
        JBro::String again;
        Check(JBro::WriteProjectFileText(project, written.c_str(), written.size(), again, error) && again == written,
            "and saving again changes nothing");
        project.scriptOutputLibraryPath.clear();
        project.build.productName.clear();
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error), "the emptied values write");
        Check(written.find("ScriptOutputLibraryPath: \"\"\n") != JBro::String::npos && written.find("  ProductName: \"\"\n") != JBro::String::npos,
            "an emptied value is written as an empty string");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.scriptOutputLibraryPath.empty()
                && reread.build.productName.empty(),
            "and reads back empty instead of the default");
        // 원문 없이 새로 적어도 같다(게임 빌드의 프로젝트 사본).
        Check(JBro::WriteProjectFileText(project, "", 0, written, error) && JBro::ParseProjectFile(written.c_str(), written.size(), reread, error)
                && reread.scriptOutputLibraryPath.empty(),
            "a new file keeps an empty value empty");
    }

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

    // **물리 스레드 설정(D-223).** `Build.PhysicsThreads` 는 Auto·Single·워커 수다. 기본은 Auto 이고, 손대지 않은 파일에는 적지 않는다.
    void TestThePhysicsThreadsSetting()
    {
        const auto parseWith = [](const char* line, JBro::ProjectFile& project, JBro::ProjectFileError& error) {
            JBro::String text = "Version: 1\nEngineVersion: 1.0.0\nFramework: 2D\nBuild:\n  ProductName: Probe\n";
            text += line;
            return JBro::ParseProjectFile(text.c_str(), text.size(), project, error);
        };
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(parseWith("", project, error) && project.build.physicsThreadMode == JBro::PhysicsThreadMode::Auto,
            "a project without the key is Auto");
        Check(parseWith("  PhysicsThreads: Single\n", project, error)
            && project.build.physicsThreadMode == JBro::PhysicsThreadMode::Single, "Single reads as the main thread alone");
        Check(parseWith("  PhysicsThreads: 3\n", project, error)
            && project.build.physicsThreadMode == JBro::PhysicsThreadMode::Workers && project.build.physicsWorkers == 3,
            "a number reads as that many workers");
        Check(parseWith("  PhysicsThreads: 0\n", project, error)
            && project.build.physicsThreadMode == JBro::PhysicsThreadMode::Single, "and zero as Single");
        Check(false == parseWith("  PhysicsThreads: many\n", project, error), "anything else is refused, not guessed");

        // 쓰기: 기본값은 없던 자리에 새로 적지 않고, 바꾸면 적히고, 다시 Auto 로 돌리면 그 줄이 Auto 가 된다.
        const char* text = "Version: 1\nEngineVersion: 1.0.0\nFramework: 2D\nBuild:\n  ProductName: Probe\n";
        JBro::ProjectFile edited;
        Check(JBro::ParseProjectFile(text, std::strlen(text), edited, error), "the probe parses");
        JBro::String written;
        Check(JBro::WriteProjectFileText(edited, text, std::strlen(text), written, error)
            && written.find("PhysicsThreads") == JBro::String::npos, "an untouched Auto is not written");
        edited.build.physicsThreadMode = JBro::PhysicsThreadMode::Workers;
        edited.build.physicsWorkers = 2;
        Check(JBro::WriteProjectFileText(edited, text, std::strlen(text), written, error)
            && written.find("  PhysicsThreads: 2\n") != JBro::String::npos, "two workers are written into the Build block");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error)
            && reread.build.physicsThreadMode == JBro::PhysicsThreadMode::Workers && reread.build.physicsWorkers == 2,
            "and read back");
        edited.build.physicsThreadMode = JBro::PhysicsThreadMode::Auto;
        JBro::String back;
        Check(JBro::WriteProjectFileText(edited, written.c_str(), written.size(), back, error)
            && back.find("  PhysicsThreads: Auto\n") != JBro::String::npos, "turning it back to Auto rewrites the line");
    }

    // **시간 설정(D-242).** 최상위 `FixedDeltaTime`·`MaxFixedSteps`·`MaxDeltaTime`·`RandomSeed` 다. 범위를 벗어나면 거절하고,
    // 기본값이면 없던 자리에 적지 않는다.
    void TestTheTimeSettings()
    {
        const auto parseWith = [](const char* lines, JBro::ProjectFile& project, JBro::ProjectFileError& error) {
            JBro::String text = "Version: 1\nEngineVersion: 1.0.0\nFramework: 2D\n";
            text += lines;
            return JBro::ParseProjectFile(text.c_str(), text.size(), project, error);
        };
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(parseWith("", project, error) && project.fixedDeltaTime == 1.0f / 60.0f && project.maxFixedSteps == 4
                && project.maxDeltaTime == 0.25f && project.randomSeed == 0,
            "a project without the keys has the engine defaults");
        Check(parseWith("FixedDeltaTime: 0.02\nMaxFixedSteps: 8\nMaxDeltaTime: 0.5\nRandomSeed: 18446744073709551615\n",
                  project, error)
                && project.fixedDeltaTime == 0.02f && project.maxFixedSteps == 8 && project.maxDeltaTime == 0.5f
                && project.randomSeed == 18446744073709551615ull,
            "the four keys are read, the seed as a full 64-bit number");
        Check(false == parseWith("FixedDeltaTime: 0\n", project, error), "a zero step is refused");
        Check(false == parseWith("FixedDeltaTime: 2\n", project, error), "a step over a second is refused");
        Check(false == parseWith("MaxFixedSteps: 0\n", project, error), "zero steps per frame is refused");
        Check(false == parseWith("MaxFixedSteps: 65\n", project, error), "more than 64 steps per frame is refused");
        Check(false == parseWith("MaxDeltaTime: -1\n", project, error), "a negative ceiling is refused");
        Check(false == parseWith("MaxDeltaTime: 11\n", project, error), "a ceiling over ten seconds is refused");
        Check(false == parseWith("RandomSeed: -3\n", project, error), "a negative seed is refused, not wrapped");
        Check(false == parseWith("RandomSeed: many\n", project, error), "a seed that is not a number is refused");

        const char* text = "Version: 1\nEngineVersion: 1.0.0\nFramework: 2D\n";
        JBro::ProjectFile edited;
        Check(JBro::ParseProjectFile(text, std::strlen(text), edited, error), "the probe parses");
        JBro::String written;
        const auto hasTimeKey = [](const JBro::String& file) {
            return file.find("FixedDeltaTime") != JBro::String::npos || file.find("MaxFixedSteps") != JBro::String::npos
                || file.find("MaxDeltaTime") != JBro::String::npos || file.find("RandomSeed") != JBro::String::npos;
        };
        Check(JBro::WriteProjectFileText(edited, text, std::strlen(text), written, error) && false == hasTimeKey(written),
            "an untouched project grows no time keys");
        edited.fixedDeltaTime = 0.02f;
        edited.randomSeed = 1234u;
        Check(JBro::WriteProjectFileText(edited, text, std::strlen(text), written, error)
                && written.find("FixedDeltaTime: 0.0199999996") != JBro::String::npos
                && written.find("RandomSeed: 1234\n") != JBro::String::npos
                && written.find("MaxFixedSteps") == JBro::String::npos,
            "changed values are written and the defaults next to them are not");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error)
                && reread.fixedDeltaTime == 0.02f && reread.randomSeed == 1234u,
            "and they read back exactly");
    }

    // **물리 레이어 이름과 충돌 표(D-233).** 이름은 자리가 비트 번호라 가운데 빈 칸이 `""` 로 남고 끝의 빈 칸은 적지 않는다.
    // 쌍은 작은 번호가 앞으로 맞춰지고, 틀린 쌍은 거절된다. 둘 다 비면 키를 적지 않는다.
    void TestThePhysicsLayerSettings()
    {
        const char* text =
            "EngineVersion: 1.0.0\n"
            "Framework: 2D\n"
            "PhysicsLayers:\n"
            "  - Default\n"
            "  - \"\"\n"
            "  - Enemy\n"
            "PhysicsIgnoredLayerPairs:\n"
            "  - 2 0\n"
            "SomeFutureKey: keep me\n";
        JBro::ProjectFile project;
        JBro::ProjectFileError error;
        Check(JBro::ParseProjectFile(text, std::strlen(text), project, error), "the physics layer settings parse");
        Check(project.physicsLayers.Size() == 3 && project.physicsLayers[0] == "Default" && project.physicsLayers[1].empty()
                && project.physicsLayers[2] == "Enemy",
            "layer names keep their slots, an unnamed one included");
        Check(project.physicsIgnoredLayerPairs.Size() == 1 && project.physicsIgnoredLayerPairs[0].first == 0
                && project.physicsIgnoredLayerPairs[0].second == 2,
            "a pair is read with the smaller layer first");

        project.physicsLayers.Add(JBro::String());
        project.physicsLayers.Add(JBro::String());
        project.physicsIgnoredLayerPairs.Add({ 1, 1 });
        JBro::String written;
        Check(JBro::WriteProjectFileText(project, text, std::strlen(text), written, error), "the layers rewrite");
        Check(written.find("PhysicsLayers:\n  - Default\n  - \"\"\n  - Enemy\nPhysicsIgnoredLayerPairs:") != JBro::String::npos,
            "trailing unnamed layers are not written, the one between is");
        Check(written.find("PhysicsIgnoredLayerPairs:\n  - 0 2\n  - 1 1\n") != JBro::String::npos, "the pairs are written as numbers");
        Check(written.find("SomeFutureKey: keep me") != JBro::String::npos, "the key after them stays");
        JBro::ProjectFile reread;
        Check(JBro::ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.physicsLayers.Size() == 3
                && reread.physicsIgnoredLayerPairs.Size() == 2,
            "and they read back");

        std::uint32_t rows[32] = {};
        JBro::ResolvePhysicsIgnoredLayers(reread, rows);
        Check(rows[0] == (1u << 2) && rows[2] == 1u && rows[1] == (1u << 1) && rows[3] == 0u,
            "the pairs become a symmetric table");

        const char* bad = "EngineVersion: 1.0.0\nFramework: 2D\nPhysicsIgnoredLayerPairs:\n  - 0 32\n";
        Check(false == JBro::ParseProjectFile(bad, std::strlen(bad), project, error), "a layer past 31 is refused");
        const char* single = "EngineVersion: 1.0.0\nFramework: 2D\nPhysicsIgnoredLayerPairs:\n  - 4\n";
        Check(false == JBro::ParseProjectFile(single, std::strlen(single), project, error), "and so is a lone number");

        const char* bare = "EngineVersion: 1.0.0\nFramework: 2D\n";
        JBro::ProjectFile none;
        Check(JBro::ParseProjectFile(bare, std::strlen(bare), none, error), "the bare file parses");
        none.physicsLayers.Add(JBro::String());
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error)
                && written.find("Physics") == JBro::String::npos,
            "a project with no named layer and no pair grows no physics keys");
        none.physicsLayers.Add("Player");
        Check(JBro::WriteProjectFileText(none, bare, std::strlen(bare), written, error)
                && written.find("PhysicsLayers:\n  - \"\"\n  - Player\n") != JBro::String::npos
                && written.find("PhysicsIgnoredLayerPairs") == JBro::String::npos,
            "a named layer is appended with the empty slot before it");
    }
}

int RunProjectFileTests()
{
    TestTheTimeSettings();
    TestThePhysicsThreadsSetting();
    TestThePhysicsLayerSettings();
    TestAnEmptyStringIsAValueNotABlock();
    TestReadsTheLegacyProjectShape();
    TestRefusesARealLegacyProjectFileIfPresent();
    TestRefusesWhatItDoesNotUnderstand();
    TestRequiresTheEngineVersionAndTheFramework();
    TestScriptModulePathResolution();
    TestDefaultsSurviveAnEmptyProject();
    TestRewritingKeepsWhatItDoesNotKnow();
    TestRewritingTheIgnorePatterns();
    TestTheProjectFontList();
    TestTheLocaleSettings();
    TestAnEmptiedValueStaysEmpty();
    TestSavingTwiceDoesNotGrowTheFile();
    TestSavingCollapsesKeysThatWereWrittenTwice();
    TestCreatesANewProject();
    TestPathsAreMadeRelativeToTheProject();
    std::cout << "Project file tests passed.\n";
    return 0;
}
