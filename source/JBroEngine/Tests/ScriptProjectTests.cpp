#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/ScriptProject.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Runtime/ScriptErrorContext.h>
#include <JBro/Types/Uuid.h>

#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

// 사용자 게임 스크립트 프로젝트를 만드는 길(cpp-script-plan §3.3, D-266)을 본다. 만들어지는 글자가 실제로 빌드되는지는
// MSBuild 로 한 번 쟀다(계획서 §3.3 의 진행) - 여기서는 글자의 약속과 "한 번만 쓴다" 를 지킨다.

namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    Bool Contains(const String& text, const char* piece)
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

    // 이 시험만 쓰는 폴더다. 한글을 섞는다 - 사용자 프로젝트 경로에 흔하다.
    fs::path FreshFolder(const char* name)
    {
        const fs::path root = fs::temp_directory_path() / fs::path(u8"JBro스크립트") / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root, ignored);
        return root;
    }

    class TakenNameProbe final : public GameScriptBase
    {
        JBRO_SCRIPT_BODY(TakenNameProbe)
    };

    void TestTheScriptFilesSayWhatTheUserAskedFor()
    {
        Array<ScriptProject::FieldSpec> fields;
        fields.Add({String("Speed"), ScriptProject::FieldType::Float});
        fields.Add({String("Alive"), ScriptProject::FieldType::Bool});
        fields.Add({String("Target"), ScriptProject::FieldType::GameObject});

        const String header = ScriptProject::MakeScriptHeader("Player", fields, FrameworkKind::Framework2D);
        Check(Contains(header, "class Player final : public GameScript2D\r\n"), "a 2D script derives from GameScript2D");
        Check(Contains(header, "    JBRO_SCRIPT_BODY(Player)\r\n"), "the body macro names the class");
        Check(Contains(header, "    JBRO_FIELD(Float, Speed) = 0.0f;\r\n")
                && Contains(header, "    JBRO_FIELD(Bool, Alive) = false;\r\n")
                && Contains(header, "    JBRO_FIELD(GameObject, Target) = {};\r\n"),
            "every field is declared with a value, never left uninitialised");
        const char* const hooks[] = {"OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy"};
        const String source = ScriptProject::MakeScriptSource("Player", FrameworkKind::Framework2D);
        for (const char* hook : hooks)
        {
            const String declaration = String("    void ") + hook + "() override;";
            const String definition = String("void Player::") + hook + "()";
            Check(Contains(header, declaration.c_str()) && Contains(source, definition.c_str()),
                "the five lifecycle hooks are declared and defined, as the old engine did");
        }
        Check(Contains(source, "JBRO_REGISTER_SCRIPT_2D(Player);"), "the source registers the script in one line");

        // 빌트인 컴포넌트도 필드로 고른다(D-302 (17)). 프렐류드가 연 핸들 이름 그대로 적힌다.
        Array<ScriptProject::FieldSpec> handles;
        handles.Add({String("Follow"), ScriptProject::FieldType::Transform2D});
        handles.Add({String("Voice"), ScriptProject::FieldType::AudioSource});
        const String handleHeader = ScriptProject::MakeScriptHeader("Chaser", handles, FrameworkKind::Framework2D);
        Check(Contains(handleHeader, "    JBRO_FIELD(Transform2D, Follow) = {};\r\n")
                && Contains(handleHeader, "    JBRO_FIELD(AudioSource, Voice) = {};\r\n"),
            "a component handle field is written under the handle's own name");
        // 다른 차원의 핸들은 그 프렐류드에 없어 컴파일되지 않는다 - 그 프로젝트에는 내놓지 않는다.
        using ScriptProject::FieldType;
        using ScriptProject::IsFieldTypeOffered;
        Check(IsFieldTypeOffered(FieldType::Transform2D, FrameworkKind::Framework2D)
                && false == IsFieldTypeOffered(FieldType::Transform2D, FrameworkKind::Framework3D)
                && IsFieldTypeOffered(FieldType::Transform3D, FrameworkKind::Framework3D)
                && false == IsFieldTypeOffered(FieldType::Transform3D, FrameworkKind::Framework2D),
            "a 2D handle is offered only to 2D projects and a 3D handle only to 3D ones");
        Check(IsFieldTypeOffered(FieldType::AudioSource, FrameworkKind::Framework2D)
                && IsFieldTypeOffered(FieldType::AudioSource, FrameworkKind::Framework3D)
                && IsFieldTypeOffered(FieldType::GameObject, FrameworkKind::Framework3D),
            "the shared handle and the plain types are offered to both");
        // 모든 종류가 제 이름을 가진다. 빠진 `case` 는 기본값 `Float` 로 떨어져 사용자가 고른 것과 다른 필드가 된다.
        for (Int32 index = static_cast<Int32>(FieldType::Asset) + 1; index < static_cast<Int32>(FieldType::Count); ++index)
        {
            Check(std::strcmp(ScriptProject::FieldCppType(static_cast<FieldType>(index.Get())), "Float") != 0,
                "every handle field type has its own C++ name");
        }

        // 2D 프렐류드는 `Vector3` 의 설명자를 들이지 않는다. 그 필드가 있을 때만 더 들인다(MSBuild 로 확인했다).
        Check(false == Contains(header, "Math3DReflection.h"), "a script without a Vector3 field does not pull in 3D reflection");
        Array<ScriptProject::FieldSpec> spatial;
        spatial.Add({String("Offset"), ScriptProject::FieldType::Vector3});
        Check(Contains(ScriptProject::MakeScriptHeader("Mover", spatial, FrameworkKind::Framework2D),
                "#include <JBro/Reflection/Math3DReflection.h>\r\n"),
            "a Vector3 field brings the header that describes it");

        const String header3D = ScriptProject::MakeScriptHeader("Orbit", {}, FrameworkKind::Framework3D);
        const String source3D = ScriptProject::MakeScriptSource("Orbit", FrameworkKind::Framework3D);
        Check(Contains(header3D, ": public GameScriptBase") && Contains(source3D, "JBRO_REGISTER_SCRIPT_3D(Orbit);"),
            "a 3D script uses the 3D base and registration");
        Check(Contains(ScriptProject::MakeModuleSource(FrameworkKind::Framework3D), "JBRO_SCRIPT_MODULE_3D()"),
            "and the 3D entry point");
    }

    void TestANameThatCannotBeATypeIsRefused()
    {
        WindowsPlatform platform;
        const fs::path folder = FreshFolder("names");
        const String where = Utf8(folder);
        using Problem = ScriptProject::NameProblem;
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "") == Problem::Empty, "an empty name");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "2Fast") == Problem::NotIdentifier, "a leading digit");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "My Script") == Problem::NotIdentifier, "a space");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), reinterpret_cast<const char*>(u8"플레이어")) == Problem::NotIdentifier,
            "a name that is not a C++ identifier");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "class") == Problem::Reserved, "a C++ keyword");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "OnUpdate") == Problem::Reserved,
            "a name the script base already uses");

        // 로드된 스크립트와 같은 이름은 캔버스 파일에서 두 타입이 갈리지 않는다.
        Check(RegisterScriptType<TakenNameProbe>(), "the probe type registers");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "TakenNameProbe") == Problem::TakenByType,
            "a name a loaded script already has");
        ScriptRegistry::Local().Clear();
        PropertyRegistry::ScriptLocal().Clear();

        // **번호를 붙여 피하지 않는다.** 이 이름이 저장되는 타입 이름이다.
        std::ofstream(folder / "Player.cpp", std::ios::binary) << "// already here\n";
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "Player") == Problem::TakenByFile,
            "a name whose file is already in the folder");
        Array<ScriptProject::FieldSpec> none;
        String error;
        Check(false == ScriptProject::CreateScript(platform, where.c_str(), "Player", none, FrameworkKind::Framework2D, error)
                && ReadAll(folder / "Player.cpp") == "// already here\n" && false == fs::exists(folder / "Player.h"),
            "and creating it anyway writes nothing");
        Check(ScriptProject::CheckScriptName(platform, where.c_str(), "Enemy") == Problem::None, "a fresh name is fine");

        Array<ScriptProject::FieldSpec> fields;
        fields.Add({String("Speed"), ScriptProject::FieldType::Float});
        fields.Add({String("Speed"), ScriptProject::FieldType::Int});
        fields.Add({String("OnStart"), ScriptProject::FieldType::Int});
        Check(ScriptProject::CheckFieldName(fields, 0) == Problem::Duplicate
                && ScriptProject::CheckFieldName(fields, 2) == Problem::Reserved,
            "field names are checked the same way, and must not repeat");
        Check(false == ScriptProject::CreateScript(platform, where.c_str(), "Enemy", fields, FrameworkKind::Framework2D, error)
                && false == fs::exists(folder / "Enemy.h"),
            "a script with a bad field is not written");
    }

    // **프로젝트 파일은 한 번만 쓴다**(기존 엔진의 C2). 사용자가 고친 파일을 에디터가 덮지 않는다.
    void TestTheProjectIsWrittenOnceAndThenLeftAlone()
    {
        WindowsPlatform platform;
        const fs::path contents = FreshFolder("once") / "Contents";
        const String where = Utf8(contents);
        String error;
        Check(ScriptProject::EnsureProject(platform, where.c_str(), FrameworkKind::Framework2D, error), "the project is created");
        for (const char* name : {"GameScript.vcxproj", "GameScript.sln", "Scripts/ScriptModule.cpp", ".gitignore"})
        {
            Check(fs::exists(contents / name), "every project file is there");
        }
        const std::string project = ReadAll(contents / "GameScript.vcxproj");
        const std::size_t open = project.find("<ProjectGuid>");
        Check(open != std::string::npos, "the project has a GUID");
        const std::string guid = project.substr(open + 13, 38);
        Check(ReadAll(contents / "GameScript.sln").find(guid) != std::string::npos, "and the solution points at the same GUID");
        Check(project.find("Scripts\\**\\*.cpp") != std::string::npos,
            "the project picks up every script by wildcard, so adding one never rewrites it");
        Check(ReadAll(contents / ".gitignore").find("JBroEngine.props") != std::string::npos,
            "the machine-specific engine location stays out of version control");

        std::ofstream(contents / "GameScript.vcxproj", std::ios::app) << "<!-- the user's own edit -->\n";
        std::ofstream(contents / "Scripts" / "ScriptModule.cpp", std::ios::app) << "// the user's own line\n";
        std::ofstream(contents / ".gitignore", std::ios::app) << "Build/\n";
        const std::string edited = ReadAll(contents / "GameScript.vcxproj");
        const std::string editedModule = ReadAll(contents / "Scripts" / "ScriptModule.cpp");
        const std::string editedIgnore = ReadAll(contents / ".gitignore");
        fs::remove(contents / "GameScript.sln");
        Check(ScriptProject::EnsureProject(platform, where.c_str(), FrameworkKind::Framework2D, error), "ensuring again succeeds");
        Check(ReadAll(contents / "GameScript.vcxproj") == edited, "and does not touch the user's project file");
        Check(ReadAll(contents / "Scripts" / "ScriptModule.cpp") == editedModule && ReadAll(contents / ".gitignore") == editedIgnore,
            "nor the entry point or ignore list the user changed");
        Check(false == fs::exists(contents / "GameScript.sln"),
            "nor writes a solution whose GUID would not match the project that is already there");
    }

    // **엔진 위치는 에디터가 열 때마다 맞춘다**(D-266). 같으면 쓰지 않는다 - 매번 쓰면 파일 감시·빌드가 헛돈다.
    void TestTheEnginePropsFollowTheEngine()
    {
        WindowsPlatform platform;
        const fs::path contents = FreshFolder("props");
        const String where = Utf8(contents);
        Check(ScriptProject::RefreshEngineProps(platform, where.c_str(), "C:/Engines/JBro/source/JBroEngine"),
            "the engine location is written");
        const std::string first = ReadAll(contents / "JBroEngine.props");
        Check(first.find(">C:\\Engines\\JBro\\source\\JBroEngine\\<") != std::string::npos,
            "as a Windows folder with a trailing separator, which the project joins file names onto");

        // 읽기 전용으로 두면 쓰려는 순간 실패한다. 같은 내용이면 쓰지 않으므로 성공이어야 한다.
        const fs::path props = contents / "JBroEngine.props";
        SetFileAttributesW(props.c_str(), FILE_ATTRIBUTE_READONLY);
        const Bool unchanged = ScriptProject::RefreshEngineProps(platform, where.c_str(), "C:/Engines/JBro/source/JBroEngine");
        SetFileAttributesW(props.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(unchanged, "the same location does not write the file again");

        Check(ScriptProject::RefreshEngineProps(platform, where.c_str(), "D:/Moved/JBroEngine")
                && ReadAll(props).find(">D:\\Moved\\JBroEngine\\<") != std::string::npos,
            "a moved engine is written over the old location");
    }

    void TestTheEngineIsFoundFromWhereTheEditorRuns()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform starts");
        const String root = ScriptProject::FindEngineRoot(platform, platform.GetExecutableFolder().c_str());
        Check(false == root.empty() && fs::exists(fs::path(std::u8string(root.begin(), root.end())) / "JBro.GameScript.targets"),
            "a development build finds the engine folder that holds the script SDK files");
        const fs::path empty = FreshFolder("nowhere");
        Check(ScriptProject::FindEngineRoot(platform, Utf8(empty).c_str()).empty(),
            "a folder with no engine above it finds nothing, rather than a wrong folder");
        platform.Shutdown();
    }

    // 에디터를 거친 길: 스크립트 프로젝트가 없는 프로젝트에서 처음 만들면 프로젝트와 엔진 위치까지 선다.
    struct LineScan
    {
        bool jscript = false;
        int total = 0;
        int jscriptLines = 0;
    };

    BOOL CALLBACK OnSourceLine(PSRCCODEINFOW info, PVOID user)
    {
        auto* scan = static_cast<LineScan*>(user);
        scan->total = scan->total + 1;
        const std::size_t length = std::wcslen(info->FileName);
        if (length >= 8 && _wcsicmp(info->FileName + length - 8, L".jscript") == 0)
        {
            scan->jscriptLines = scan->jscriptLines + 1;
            if (info->Address != 0)
            {
                scan->jscript = true;
            }
        }
        return TRUE;
    }

    // **`.jscript` 소스 맵**(D-330): 이미터가 문장마다 `#line <줄> "<파일>.jscript"` 를 넣어, 생성 C++ 를 컴파일한 PDB 의 줄 기록이
    // `.jscript` 파일·줄을 가리킨다 - `.cpp` 가 아니라. 디버거는 그 PDB 로 `.jscript` 줄에 바로 중단점을 건다. 실제로 그 DLL 의 PDB 가
    // `.jscript` 줄을 코드 주소로 매핑하는지 본다. 공유 심볼 세션(플랫폼의 `DescribeCodeAddress`)을 건드리지 않게 별도 핸들로 연다.
    Bool JScriptLinesAreAddressable(const fs::path& dll)
    {
        const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4a42524f));
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
        if (FALSE == SymInitializeW(handle, nullptr, FALSE))
        {
            return false;
        }
        const DWORD64 base = SymLoadModuleExW(handle, nullptr, dll.wstring().c_str(), nullptr, 0, 0, nullptr, 0);
        LineScan scan;
        if (base != 0)
        {
            SymEnumLinesW(handle, base, nullptr, nullptr, &OnSourceLine, &scan);
            SymUnloadModule64(handle, base);
        }
        SymCleanup(handle);
        return scan.jscript;
    }

    void TestTheEditorCreatesAScriptAndItsProject()
    {
        const fs::path root = FreshFolder("editor");
        fs::create_directories(root / "Contents" / "Assets");
        const fs::path projectPath = root / "Scripted.jproject";
        std::ofstream(projectPath, std::ios::binary) <<
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Contents/Assets\n"
            "ScriptSourceDirectory: Contents\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "Build:\n"
            "  ProductName: Scripted\n";

        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; creating a script from the editor not verified" << std::endl;
            return;
        }
        ProjectFileError error;
        Check(editor.OpenProjectFile(Utf8(projectPath).c_str(), error), "the project opens");
        Check(false == fs::exists(root / "Contents" / "JBroEngine.props"),
            "opening a project that has no script project writes nothing into it");

        Array<ScriptProject::FieldSpec> fields;
        fields.Add({String("Speed"), ScriptProject::FieldType::Float});
        String failure;
        const UInt64 before = editor.GetScriptFilesRevision();
        const String created = editor.CreateScript("", "Player", fields, failure);
        Check(false == created.empty(), "the editor creates the script");
        const fs::path contents = root / "Contents";
        Check(fs::exists(contents / "Scripts" / "Player.h") && fs::exists(contents / "Scripts" / "Player.cpp")
                && fs::exists(contents / "GameScript.vcxproj") && fs::exists(contents / "JBroEngine.props"),
            "with the script project and the engine location around it");
        Check(editor.GetScriptFilesRevision() != before, "the asset browser is told to look again");

        Array<String> folders;
        Array<String> files;
        editor.CollectScriptFiles(folders, files);
        Bool listed = false;
        for (const String& file : files)
        {
            listed = listed || file == "Player.h";
        }
        Check(listed, "the new header is listed under the script root");
        std::ofstream(contents / "Scripts" / "Guard.jscript") << "script Guard {}\n";
        editor.MarkScriptFilesChanged();
        editor.CollectScriptFiles(folders, files);
        Bool listedJBroScript = false;
        for (const String& file : files)
        {
            listedJBroScript = listedJBroScript || file == "Guard.jscript";
        }
        Check(listedJBroScript, "JBroScript files are listed under the editor's Scripts root");
        Check(editor.CheckScriptName("", "Guard") == ScriptProject::NameProblem::TakenByFile,
            "the C++ script wizard will not reuse a JBroScript file stem");

        Check(editor.CheckScriptName("", "Player") == ScriptProject::NameProblem::TakenByFile,
            "the same name cannot be created twice");

        // 창은 에디터의 팝업 관리자(`EditorPopup`)로 뜨고, 그려지는 동안 에셋 브라우저도 스크립트 자리를 함께 그린다.
        Check(editor.EnableEditorUi({64, 48}), "the editor UI turns on");
        editor.OpenNewScriptPopup("");
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor ticks with the new script window up");
        }
        Check(editor.IsPopupOpenById("new_script"), "the new script window is open");
        const auto waitForBuild = [&editor]() {
            for (Int32 step = 0; step < 3000; ++step)
            {
                Check(editor.Tick(1.0f / 60.0f), "the editor keeps ticking while scripts build");
                if (editor.GetScriptBuildState() != EditorApplication::ScriptBuildState::Running)
                {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check(false, "the editor's script build finishes within 30 seconds");
        };
        Check(editor.BuildScripts(), "the editor starts a mixed C++ and JBroScript build");
        waitForBuild();
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded
                && fs::exists(root / "x64" / "Debug" / "GameScript.dll"),
            "the editor builds the generated JBroScript into the game DLL");
        // 사용자 프로젝트는 엔진 소스가 아니라 스크립트 SDK 사본을 include 한다. 컴포넌트 데이터 헤더는 핸들을 쓰라는 `#error` 이고,
        // 스크립트 입구에서 닿지 않는 엔진 헤더는 없는 파일이다.
        const fs::path leak = contents / "Scripts" / "Leak.cpp";
        const auto buildFailsWith = [&](const char* header, const char* code, const char* where) {
            std::ofstream(leak, std::ios::trunc) << "#include <" << header << ">\n";
            Check(editor.BuildScripts(), "the editor starts a build that includes an engine header");
            waitForBuild();
            Bool found = false;
            for (const ScriptBuild::Diagnostic& diagnostic : editor.GetScriptDiagnostics())
            {
                found = found || (diagnostic.code == code && diagnostic.file.find(where) != String::npos);
            }
            return editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Failed && found;
        };
        Check(buildFailsWith("JBro/Framework2D/Component/Transform2D.h", "C1189", "ScriptSdk"),
            "a component data header stops a user build with the hint to use its handle");
        Check(buildFailsWith("JBro/Core/Log.h", "C1083", "Leak.cpp"),
            "an engine header outside the script SDK cannot be included by a user script");
        fs::remove(leak);
        const fs::path generatedDirectory = root / "x64" / "Intermediate" / "Debug" / "JBroScript";
        const auto generatedHasType = [&generatedDirectory](const char* type) {
            for (const auto& file : fs::directory_iterator(generatedDirectory))
            {
                if (file.path().extension() == L".h" && ReadAll(file.path()).find(type) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        };
        const fs::path follower = contents / "Scripts" / "Follower.jscript";
        std::ofstream(follower) << "script Follower { public ref Guard peer }\n";
        Check(editor.BuildScripts(), "the editor starts a build after adding a second JBroScript file");
        waitForBuild();
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded
                && generatedHasType("class Follower"),
            "the second JBroScript file joins the game DLL without editing the project file");
        fs::path guardObject;
        fs::path followerObject;
        for (const auto& file : fs::directory_iterator(generatedDirectory))
        {
            if (file.path().extension() != L".cpp")
            {
                continue;
            }
            const std::string text = ReadAll(file.path());
            const fs::path object = generatedDirectory.parent_path() / (file.path().stem().native() + L".obj");
            if (text.find("operator()<Guard>()") != std::string::npos)
            {
                guardObject = object;
            }
            if (text.find("operator()<Follower>()") != std::string::npos)
            {
                followerObject = object;
            }
        }
        Check(fs::exists(guardObject) && fs::exists(followerObject), "MSBuild compiles a separate object for each source");
        const auto guardTime = fs::last_write_time(guardObject);
        const auto followerTime = fs::last_write_time(followerObject);
        std::ofstream(follower, std::ios::trunc)
            << "script Follower {\n public ref Guard peer\n fn Score() -> Int { return 7 }\n}\n";
        Check(editor.BuildScripts(), "the editor builds a change in one JBroScript source");
        waitForBuild();
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded
                && fs::last_write_time(guardObject) == guardTime && fs::last_write_time(followerObject) != followerTime,
            "an unrelated script object is not recompiled when one source changes");
        // **`.jscript` 소스 맵**(D-330): 생성 C++ 의 `#line` 으로 PDB 가 `.jscript` 줄을 코드 주소로 매핑한다 - `.cpp` 가 아니라.
        // Follower 의 `return 7` 같은 실제 문장이 `.jscript` 줄 기록을 남겨, 디버거가 그 줄에 바로 중단점을 건다.
        Check(JScriptLinesAreAddressable(root / "x64" / "Debug" / "GameScript.dll"),
            "the built DLL's PDB maps JBroScript lines to code addresses, so breakpoints sit on .jscript lines");
        fs::remove(follower);
        Check(editor.BuildScripts(), "the editor starts a build after deleting a JBroScript file");
        waitForBuild();
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded
                && !generatedHasType("class Follower"),
            "a deleted source is removed from generated C++ even if remaining files are older");
        std::ofstream(contents / "Scripts" / "Guard.jscript", std::ios::trunc)
            << "script Guard { fn OnUpdate() override { if true {} } }\n";
        Check(editor.BuildScripts(), "the editor starts a build after JBroScript changes");
        waitForBuild();
        Bool sourceDiagnostic = false;
        for (const ScriptBuild::Diagnostic& diagnostic : editor.GetScriptDiagnostics())
        {
            sourceDiagnostic = sourceDiagnostic || (diagnostic.file.find("Guard.jscript") != String::npos
                && diagnostic.line.Get() > 0 && diagnostic.isError);
        }
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Failed && sourceDiagnostic,
            "an invalid JBroScript source appears at its own file and line in Build Results");
        fs::remove(contents / "Scripts" / "Guard.jscript");
        Check(editor.BuildScripts(), "the editor builds after removing the last JBroScript source");
        waitForBuild();
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded
                && ReadAll(generatedDirectory / "JBroGenerated.items").empty() && !generatedHasType("class Guard"),
            "the last removed source leaves the C++ game project with no generated compile inputs");
        const HMODULE library = LoadLibraryW((root / "x64" / "Debug" / "GameScript.dll").c_str());
        Check(library != nullptr, "the game DLL still opens after all generated scripts are removed");
        const auto getApi = reinterpret_cast<GetScriptModuleApiFunction>(GetProcAddress(library, ScriptModuleEntryPointName));
        const ScriptModuleApi* api = getApi != nullptr
            ? getApi(ScriptModuleAbiVersion, static_cast<UInt32>(sizeof(ScriptModuleApi))) : nullptr;
        Bool removedGeneratedMarker = api != nullptr;
        if (api != nullptr)
        {
            for (UInt32 index = 0; index < api->RequiredContextCount; ++index)
            {
                removedGeneratedMarker = removedGeneratedMarker && api->RequiredContexts[index].TypeId != ScriptErrorContextTypeId;
            }
        }
        FreeLibrary(library);
        Check(removedGeneratedMarker, "MSBuild relinks without stale generated objects or their mandatory error context");
        editor.Shutdown();
    }
}

Int32 RunScriptProjectTests()
{
    TestTheScriptFilesSayWhatTheUserAskedFor();
    TestANameThatCannotBeATypeIsRefused();
    TestTheProjectIsWrittenOnceAndThenLeftAlone();
    TestTheEnginePropsFollowTheEngine();
    TestTheEngineIsFoundFromWhereTheEditorRuns();
    TestTheEditorCreatesAScriptAndItsProject();
    std::cout << "Script project tests passed.\n";
    return 0;
}
