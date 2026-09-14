#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

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
}

// 에디터를 실제 창으로 띄운다. 인자로 프레임 수를 주면 그만큼만 돌고 끝난다 -
// 사람 없이 돌리는 확인용이고, 없으면 창을 닫을 때까지 돈다.
int main(int argumentCount, char** arguments)
{
    long long frameLimit = 0;
    if (argumentCount > 1)
    {
        frameLimit = std::atoll(arguments[1]);
    }

    JBro::EditorApplication editor;
    JBro::EditorApplicationConfig config;
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.windowVisible = true;
    if (false == editor.Initialize(config))
    {
        std::printf("the editor could not initialize\n");
        return 1;
    }

    JBro::ProjectDescriptor project;
    constexpr char name[] = "JBroEditorHost";
    project.name = {name, sizeof(name) - 1};
    if (false == editor.OpenProject(project))
    {
        std::printf("the editor could not open its project\n");
        return 2;
    }

    if (JBro::Canvas* canvas = editor.GetCanvas())
    {
        PopulateProbeScene(*canvas);
    }

    if (false == editor.EnableEditorUi({GameViewWidth, GameViewHeight}))
    {
        std::printf("the editor UI could not start\n");
        return 3;
    }

    // 하나 골라 둔다. 인스펙터는 고른 것이 있어야 보여 줄 것이 있고, 띄우자마자
    // 빈 칸이면 붙었는지 아닌지 알 수 없다.
    if (JBro::Canvas* canvas = editor.GetCanvas())
    {
        canvas->ForEachObject([&editor](JBro::GameObject& object) {
            if (editor.GetSelectedObject() == nullptr
                && std::strncmp(object.GetTag(), "빨강", 6) == 0)
            {
                editor.SetSelectedObject(&object);
            }
        });
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
        if (frameLimit > 0 && frames >= frameLimit)
        {
            break;
        }
    }

    std::printf("the editor ran %lld frame(s)\n", frames);
    editor.Shutdown();
    return 0;
}
