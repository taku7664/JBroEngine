#include <JBro/Core/Log.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
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

namespace
{
    // 게임 화면의 해상도다. **에디터 창 크기와 무관하다** - 창을 끌어도 게임이 보는
    // 화면은 그대로여야 하고, 패널에는 비율을 지켜 맞춰 붙인다.
    constexpr std::uint32_t GameViewWidth = 640;
    constexpr std::uint32_t GameViewHeight = 360;

    // 종료 코드다. 런처가 이 값으로 무엇이 틀어졌는지 구분한다(D-97).
    // 사람이 읽을 사유는 표준 출력으로도 같이 나간다.
    constexpr int ExitOk = 0;
    constexpr int ExitInitializeFailed = 1;
    constexpr int ExitProjectFailed = 2;
    constexpr int ExitEditorUiFailed = 3;
    constexpr int ExitUsageError = 64;

    struct HostOptions
    {
        // 열 `.jproject` 파일이다. 널이면 프로젝트 없이 확인용 씬을 띄운다.
        const char* projectFilePath = nullptr;
        // 로컬라이징 표와 아이콘 글꼴을 찾을 기준 폴더다. 널이면 현재 작업 폴더가 기준이고,
        // 그것이 기존 동작이다. 런처는 작업 폴더를 옮기는 대신 이 값을 넘긴다.
        const char* contentRoot = nullptr;
        // 0 이면 창을 닫을 때까지 돈다. 양수면 그만큼만 돌고 끝난다 - 사람 없이 돌리는 확인용이다.
        long long frameLimit = 0;
        bool showHelp = false;
    };

    void PrintUsage()
    {
        std::printf(
            "usage: JBroEditorHost [options] [<project.jproject>]\n"
            "\n"
            "  --project <path>      open this .jproject file\n"
            "  --content-root <path> where Localization/ and ThirdParty/ live "
            "(default: the working directory)\n"
            "  --frames <count>      run this many frames and exit "
            "(default: until the window closes)\n"
            "  -h, --help            print this text\n"
            "\n"
            "exit codes: 0 ok, 1 initialize failed, 2 project failed, 3 editor UI failed, "
            "64 bad arguments\n");
    }

    bool ParseFrameLimit(const char* text, long long& result)
    {
        char* end = nullptr;
        const long long value = std::strtoll(text, &end, 10);
        if (end == text || end == nullptr || *end != '\0' || value < 0)
        {
            return false;
        }
        result = value;
        return true;
    }

    // 값을 받는 옵션이 값 없이 마지막에 오면 다음 인자를 읽다가 배열 밖으로 나간다.
    // 여기서 한 번에 막고, 무엇이 빠졌는지 옵션 이름으로 알린다.
    bool TakeValue(
        int argumentCount,
        char** arguments,
        int& index,
        const char* optionName,
        const char*& value,
        JBro::String& error)
    {
        if (index + 1 >= argumentCount)
        {
            error = "this option needs a value: ";
            error.append(optionName);
            return false;
        }
        ++index;
        value = arguments[index];
        return true;
    }

    bool ParseOptions(int argumentCount, char** arguments, HostOptions& options, JBro::String& error)
    {
        for (int index = 1; index < argumentCount; ++index)
        {
            const char* argument = arguments[index];
            if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0)
            {
                options.showHelp = true;
                return true;
            }
            if (std::strcmp(argument, "--project") == 0)
            {
                if (false
                    == TakeValue(argumentCount, arguments, index, argument, options.projectFilePath, error))
                {
                    return false;
                }
                continue;
            }
            if (std::strcmp(argument, "--content-root") == 0)
            {
                if (false
                    == TakeValue(argumentCount, arguments, index, argument, options.contentRoot, error))
                {
                    return false;
                }
                continue;
            }
            if (std::strcmp(argument, "--frames") == 0)
            {
                const char* value = nullptr;
                if (false == TakeValue(argumentCount, arguments, index, argument, value, error))
                {
                    return false;
                }
                if (false == ParseFrameLimit(value, options.frameLimit))
                {
                    error = "the frame count must be zero or more, not: ";
                    error.append(value);
                    return false;
                }
                continue;
            }
            if (argument[0] == '-' && argument[1] != '\0')
            {
                error = "this option is not known: ";
                error.append(argument);
                return false;
            }
            // 이름 없는 인자는 프로젝트 경로다. 런처가 아니라 사람이 손으로 열 때 쓴다.
            if (options.projectFilePath != nullptr)
            {
                error = "the project was given twice: ";
                error.append(argument);
                return false;
            }
            options.projectFilePath = argument;
        }
        return true;
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
        JBro::GameObject* eye = canvas.CreateObject("Camera");
        canvas.AttachComponent<JBro::Component::Transform2D>(eye);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(eye);
        camera->primary = true;
        camera->orthographicSize = 5.0f;
        camera->clearColor = {0.12f, 0.14f, 0.20f, 1.0f};

        struct Block
        {
            const char* name;
            float x;
            float y;
            float red;
            float green;
            float blue;
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
            JBro::GameObject* object = canvas.CreateObject(block.name);
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
        canvas.ForEachObject([&editor](JBro::GameObject& object) {
            if (editor.GetSelectedObject() == nullptr
                && std::strncmp(object.GetTag(), "빨강", 6) == 0)
            {
                editor.SetSelectedObject(&object);
            }
        });
    }
}

// 에디터를 실제 창으로 띄운다. 런처는 `--project` 로 열 프로젝트를 주고, 프로젝트를 주지
// 않으면 예전처럼 확인용 씬이 뜬다. 인자 규약과 종료 코드는 D-97 이다.
int main(int argumentCount, char** arguments)
{
    HostOptions options;
    JBro::String optionError;
    if (false == ParseOptions(argumentCount, arguments, options, optionError))
    {
        std::printf("%s\n\n", optionError.c_str());
        PrintUsage();
        return ExitUsageError;
    }
    if (options.showHelp)
    {
        PrintUsage();
        return ExitOk;
    }

    const JBro::String localizationDirectory = JoinContentPath(options.contentRoot, "Localization");
    const JBro::String iconFontPath = JoinContentPath(
        options.contentRoot,
        "ThirdParty/FontAwesome/FontAwesome7-Free-Solid-900.otf");

    JBro::EditorApplication editor;
    JBro::EditorApplicationConfig config;
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.windowVisible = true;
    config.localizationDirectory = localizationDirectory.c_str();
    config.iconFontPath = iconFontPath.c_str();
    if (false == editor.Initialize(config))
    {
        std::printf("the editor could not initialize\n");
        return ExitInitializeFailed;
    }

    if (options.projectFilePath != nullptr)
    {
        JBro::ProjectFileError error;
        if (false == editor.OpenProjectFile(options.projectFilePath, error))
        {
            // 줄 번호가 0 이면 파일 자체를 열지 못한 것이다(ProjectFile.h).
            // 런처가 그대로 사람에게 보여 줄 수 있도록 경로와 같이 낸다.
            std::printf(
                "the editor could not open %s (line %u): %s\n",
                options.projectFilePath,
                error.line,
                error.message.c_str());
            editor.Shutdown();
            return ExitProjectFailed;
        }
        // 어느 엔진 버전으로 어느 차원인지는 파일이 정한다(D-99). 런처가 띄운 엔진이
        // 프로젝트가 적어 둔 버전과 다를 수 있으므로, 실제로 연 쪽이 그 값을 낸다.
        const JBro::ProjectFile& opened = editor.GetProjectFile();
        std::printf(
            "the editor opened %s (engine %s, %s)\n",
            options.projectFilePath,
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
        project.name = {name, sizeof(name) - 1};
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

    if (false == editor.EnableEditorUi({GameViewWidth, GameViewHeight}))
    {
        std::printf("the editor UI could not start\n");
        editor.Shutdown();
        return ExitEditorUiFailed;
    }

    if (options.projectFilePath == nullptr)
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
        if (options.frameLimit > 0 && frames >= options.frameLimit)
        {
            break;
        }
    }

    std::printf("the editor ran %lld frame(s)\n", frames);
    editor.Shutdown();
    return ExitOk;
}
