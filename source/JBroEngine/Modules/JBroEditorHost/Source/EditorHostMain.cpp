#include <JBro/Core/Log.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorControlPort.h>
#include <JBro/Editor/EditorHostArguments.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/String.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace
{
    // 게임 화면의 해상도다. **에디터 창 크기와 무관하다** - 창을 끌어도 게임이 보는
    // 화면은 그대로여야 하고, 패널에는 비율을 지켜 맞춰 붙인다.
    constexpr JBro::UInt32 SimulationViewWidth = 640;
    constexpr JBro::UInt32 SimulationViewHeight = 360;

    // 종료 코드다. 런처가 이 값으로 무엇이 틀어졌는지 구분한다(D-97).
    // 사람이 읽을 사유는 표준 출력으로도 같이 나간다.
    constexpr JBro::Int32 ExitOk = 0;
    constexpr JBro::Int32 ExitInitializeFailed = 1;
    constexpr JBro::Int32 ExitProjectFailed = 2;
    constexpr JBro::Int32 ExitEditorUiFailed = 3;
    // 확장자 연결을 바꾸지 못했다(D-389).
    constexpr JBro::Int32 ExitFileTypeFailed = 4;
    constexpr JBro::Int32 ExitUsageError = 64;

    void PrintUsage()
    {
        std::printf(
            "usage: JBroEditorHost [options] [<project.jproject>]\n"
            "\n"
            "  --project <path>         open this .jproject file\n"
            "  --content-root <path>    where Localization/ and ThirdParty/ live "
            "(default: the working directory)\n"
            "  --frames <count>         run this many frames and exit "
            "(default: until the window closes)\n"
            "  --control-port <port>    listen for tools on 127.0.0.1:<port>, 0 to turn it off "
            "(default: 3663)\n"
            "  --register-file-type     open .jproject files with this editor when double-clicked, "
            "for the current user, then exit\n"
            "  --unregister-file-type   undo --register-file-type, then exit\n"
            "  -h, --help               print this text\n"
            "\n"
            "exit codes: 0 ok, 1 initialize failed, 2 project failed, 3 editor UI failed, "
            "4 file type not changed, 64 bad arguments\n");
    }

    // **확장자 연결만 하고 끝낸다**(D-389). 창도 엔진도 띄우지 않는다 - 레지스트리 함수는 플랫폼의 초기화가 필요 없다.
    JBro::Int32 ChangeProjectFileType(JBro::Bool add)
    {
        JBro::WindowsPlatform platform;
        JBro::FileTypeAssociation desc;
        if (false == JBro::MakeProjectFileType(platform, desc))
        {
            std::printf("the editor could not find its own path\n");
            return ExitFileTypeFailed;
        }
        if (add)
        {
            if (false == platform.RegisterFileType(desc))
            {
                std::printf("the .jproject file type could not be registered\n");
                return ExitFileTypeFailed;
            }
            std::printf("registered .jproject: %s\n", desc.command.c_str());
            return ExitOk;
        }
        if (false == platform.UnregisterFileType(desc))
        {
            std::printf("the .jproject file type could not be removed\n");
            return ExitFileTypeFailed;
        }
        std::printf("removed the .jproject file type\n");
        return ExitOk;
    }

    // 기준 폴더에 상대경로를 붙인다. 기준이 없으면 상대경로를 그대로 둔다 -
    // 그때는 현재 작업 폴더가 기준이고, 그것이 원래 동작이다.
    JBro::String JoinContentPath(const char* contentRoot, const char* relativePath)
    {
        JBro::String result;
        if (contentRoot == nullptr || contentRoot[0] == '\0')
        {
            result = relativePath;
            return result;
        }
        result = contentRoot;
        const char last = result[result.size() - 1];
        if (last != '/' && last != '\\')
        {
            result.append("/");
        }
        result.append(relativePath);
        return result;
    }

    // 볼 것이 있어야 화면이 떴는지 알 수 있다. 색이 서로 다른 사각형 몇 개를 놓는다.
    void PopulateProbeScene(JBro::Canvas& canvas)
    {
        JBro::Object::GameObject* eye = canvas.CreateObject("Camera");
        canvas.AttachComponent<JBro::Component::Transform2D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(eye);
        camera->orthographicSize = 5.0f;
        camera->clearColor = {0.12f, 0.14f, 0.20f, 1.0f};

        struct Block
        {
            const char* name;
            JBro::Float x;
            JBro::Float y;
            JBro::Float red;
            JBro::Float green;
            JBro::Float blue;
        };
        // 이름에 한글을 섞는다. 글꼴이 안 잡혔으면 여기가 네모로 나온다 -
        // 띄워 놓고 눈으로 바로 알 수 있는 자리다.
        static const Block blocks[] = {
            {"빨강 Red", -4.0f, 0.0f, 0.90f, 0.25f, 0.25f},
            {"초록 Green", -1.5f, 1.5f, 0.25f, 0.85f, 0.35f},
            {"Blue", 1.5f, -1.5f, 0.30f, 0.45f, 0.95f},
            {"Amber", 4.0f, 0.0f, 0.95f, 0.75f, 0.20f},
        };

        for (const Block& block : blocks)
        {
            JBro::Object::GameObject* object = canvas.CreateObject(block.name);
            auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
            transform->position = {block.x, block.y};

            auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
            sprite->tint = {block.red, block.green, block.blue, 1.0f};
            sprite->size = {2.0f, 2.0f};
        }
    }

    // 확인용 씬은 하나 골라 둔다. 인스펙터는 고른 것이 있어야 보여 줄 것이 있고,
    // 띄우자마자 빈 칸이면 붙었는지 아닌지 알 수 없다.
    void SelectProbeObject(JBro::EditorApplication& editor, JBro::Canvas& canvas)
    {
        canvas.ForEachObject([&editor](JBro::Object::GameObject& object) {
            if (editor.GetSelectedObject() == nullptr
                && std::strncmp(object.GetName(), "빨강", 6) == 0)
            {
                editor.SetSelectedObject(&object);
            }
        });
    }
}

// 에디터를 실제 창으로 띄운다. 런처는 `--project` 로 열 프로젝트를 주고, 프로젝트를 주지
// 않으면 예전처럼 확인용 씬이 뜬다. 인자 규약과 종료 코드는 D-97 이다.
// `ParentConsole.cpp` 의 것이다. 터미널에서 띄웠으면 그 콘솔로 출력을 낸다.
void AttachToParentConsole();
// 같은 파일의 것이다. 넓은 문자 인자 하나를 UTF-8 로 바꾼다.
JBro::String ToUtf8Argument(const wchar_t* argument);

// **인자를 넓은 문자로 받는다**(D-389). 좁은 `main` 의 `argv` 는 이 기계의 ANSI 코드 페이지로 오고, 엔진은 경로를 UTF-8 로 읽는다 -
// 탐색기에서 `문서` 아래의 `.jproject` 를 두 번 누르면 경로가 깨져 열리지 않았다.
int wmain(int argumentCount, wchar_t** wideArguments)
{
    AttachToParentConsole();
    JBro::Array<JBro::String> utf8;
    JBro::Array<const char*> arguments;
    for (JBro::Int32 index = 0; index < argumentCount; ++index)
    {
        utf8.Add(ToUtf8Argument(wideArguments[index]));
    }
    for (std::size_t index = 0; index < utf8.Size(); ++index)
    {
        arguments.Add(utf8[index].c_str());
    }
    const JBro::EditorHostArguments options = JBro::ParseEditorHostArguments(argumentCount, arguments.Data());
    if (false == options.IsValid())
    {
        std::printf("%s\n\n", options.error.c_str());
        PrintUsage();
        return ExitUsageError;
    }
    if (options.showHelp)
    {
        PrintUsage();
        return ExitOk;
    }
    if (options.registerFileType || options.unregisterFileType)
    {
        return ChangeProjectFileType(options.registerFileType);
    }

    const char* const contentRoot = options.contentRoot.empty() ? nullptr : options.contentRoot.c_str();
    const char* const projectFilePath = options.projectFile.empty() ? nullptr : options.projectFile.c_str();
    const JBro::String localizationDirectory = JoinContentPath(contentRoot, "Localization");
    const JBro::String iconFontPath = JoinContentPath(
        contentRoot,
        "ThirdParty/MaterialDesignIcons/materialdesignicons-webfont.ttf");

    JBro::EditorApplication editor;
    JBro::EditorApplicationConfig config;
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.windowVisible = true;
    // 미리 듣기와 플레이의 소리를 낸다(D-197).
    config.audioDevice = true;
    config.localizationDirectory = localizationDirectory.c_str();
    config.iconFontPath = iconFontPath.c_str();
    // 사람의 에디터 환경설정(바꾼 단축키 등)을 읽고 쓴다(D-228). 테스트는 이것을 켜지 않는다.
    config.userPreferences = true;
    // 밖의 도구(에이전트)가 가이드를 켜는 자리다(D-270). 루프백에만 열린다.
    config.controlPort = static_cast<std::uint16_t>(options.controlPort.Get());
    if (false == editor.Initialize(config))
    {
        std::printf("the editor could not initialize\n");
        return ExitInitializeFailed;
    }

    if (projectFilePath != nullptr)
    {
        JBro::ProjectFileError error;
        if (false == editor.OpenProjectFile(projectFilePath, error))
        {
            // 줄 번호가 0 이면 파일 자체를 열지 못한 것이다(ProjectFile.h).
            // 런처가 그대로 사람에게 보여 줄 수 있도록 경로와 같이 낸다.
            std::printf(
                "the editor could not open %s (line %u): %s\n",
                projectFilePath,
                error.line.Get(),
                error.message.c_str());
            editor.Shutdown();
            return ExitProjectFailed;
        }
        // 어느 엔진 버전으로 어느 차원인지는 파일이 정한다(D-99). 런처가 띄운 엔진이
        // 프로젝트가 적어 둔 버전과 다를 수 있으므로, 실제로 연 쪽이 그 값을 낸다.
        const JBro::ProjectFile& opened = editor.GetProjectFile();
        std::printf(
            "the editor opened %s (engine %s, %s)\n",
            projectFilePath,
            opened.engineVersion.c_str(),
            opened.framework == JBro::FrameworkKind::Framework3D ? "3D" : "2D");
        // 스크립트가 안 실려도 프로젝트는 열린다(D-98). 조용히 넘어가면 사람은 스크립트가
        // 도는 줄 알고, 런처도 그 사실을 전할 길이 없다.
        if (false == editor.IsScriptModuleLoaded()
            && false == editor.GetScriptModuleError().empty())
        {
            JBro::Log::Write(JBro::LogLevel::Warning, "script", "%s",
                editor.GetScriptModuleError().c_str());
        }
    }
    else
    {
        JBro::ProjectDescriptor project;
        constexpr char name[] = "JBroEditorHost";
        project.name = {name, static_cast<JBro::UInt32>(sizeof(name) - 1)};
        if (false == editor.OpenProject(project))
        {
            std::printf("the editor could not open its project\n");
            editor.Shutdown();
            return ExitProjectFailed;
        }
        if (JBro::Canvas* canvas = editor.GetCanvas())
        {
            PopulateProbeScene(*canvas);
        }
    }

    if (false == editor.EnableEditorUi({SimulationViewWidth, SimulationViewHeight}))
    {
        std::printf("the editor UI could not start\n");
        editor.Shutdown();
        return ExitEditorUiFailed;
    }

    if (projectFilePath == nullptr)
    {
        if (JBro::Canvas* canvas = editor.GetCanvas())
        {
            SelectProbeObject(editor, *canvas);
        }
    }

    auto previous = std::chrono::steady_clock::now();
    long long frames = 0;
    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<float> elapsed = now - previous;
        previous = now;

        if (false == editor.Tick(elapsed.count()))
        {
            break;
        }
        ++frames;
        if (options.frameLimit > 0 && frames >= options.frameLimit.Get())
        {
            break;
        }
    }

    // **왜 멈췄는지 남긴다.** 창을 닫은 것인지 프레임이 실패한 것인지 모르면, 켜자마자 꺼지는 에디터를
    // 사람도 런처도 설명할 수 없다(실제로 그런 일이 있었다, D-160).
    const char* const statusNames[] = {"ready", "skipped", "surface lost", "device lost", "invalid state"};
    const auto status = static_cast<std::size_t>(editor.GetLastFrameStatus());
    std::printf("the editor ran %lld frame(s); last frame: %s\n", frames,
        status < sizeof(statusNames) / sizeof(statusNames[0]) ? statusNames[status] : "?");
    editor.Shutdown();
    return ExitOk;
}
