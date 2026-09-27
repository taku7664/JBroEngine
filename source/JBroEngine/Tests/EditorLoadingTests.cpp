#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/TaskProgress.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Task/TaskManager.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

// 에디터 로딩(공용 기반 4 번, D-217)과 상태 표시줄(13 번)의 테스트다(D-236).
// 캔버스를 열면 에셋은 워커로 가고 캔버스는 곧바로 선다 - 그림은 로드가 끝난 틱에 붙는다. 실패는 알림이 되고,
// 닫기는 도는 로드를 취소하고 기다린다. 상태 표시줄은 창 바닥 한 줄이고, 도는 묶음을 누르면 태스크 목록이 펼쳐진다.
namespace
{
    constexpr std::uint32_t WindowWidth = 640;
    constexpr std::uint32_t WindowHeight = 480;
    constexpr float Frame = 1.0f / 60.0f;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // 2x2 RGBA PNG 다(에셋 시스템 시험과 같은 것).
    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    // 자기 프로세스의 에디터 창만 찾는다(에디터 시험과 같은 까닭 - 다른 시험 프로세스의 창에 마우스를 보내지 않는다).
    HWND FindOwnEditorWindow()
    {
        struct Search
        {
            HWND found = nullptr;
            DWORD process = GetCurrentProcessId();
        } search;
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                Search& search = *reinterpret_cast<Search*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (owner != search.process)
                {
                    return TRUE;
                }
                wchar_t className[64] = {};
                wchar_t title[64] = {};
                GetClassNameW(hwnd, className, 64);
                GetWindowTextW(hwnd, title, 64);
                if (std::wcscmp(className, L"JBroEngineWindow") == 0 && std::wcscmp(title, L"JBro Editor") == 0)
                {
                    search.found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    void ClickAt(JBro::EditorApplication& editor, HWND hwnd, int x, int y)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
    }

    bool IsWindowActive(const char* name)
    {
        ImGuiWindow* window = ImGui::FindWindowByName(name);
        return window != nullptr && window->Active;
    }

    struct LoadProbe
    {
        std::filesystem::path root;
        std::filesystem::path png;
        std::filesystem::path canvas;
        std::string projectPath;

        void Make(const char* name)
        {
            root = std::filesystem::temp_directory_path() / name;
            std::error_code code;
            std::filesystem::remove_all(root, code);
            std::filesystem::create_directories(root / "Assets" / "Art", code);
            std::filesystem::create_directories(root / "Assets" / "Scenes", code);
            png = root / "Assets" / "Art" / "tiny.png";
            canvas = root / "Assets" / "Scenes" / "Opening.jcanvas";
            WritePng(true);
            const std::filesystem::path project = root / "Probe.jproject";
            std::ofstream file(project, std::ios::binary);
            file << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\n";
            projectPath = project.generic_string();
        }

        void WritePng(bool valid)
        {
            std::ofstream file(png, std::ios::binary | std::ios::trunc);
            if (valid)
            {
                file.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
            }
            else
            {
                file << "not a png";
            }
        }

        void Remove()
        {
            std::error_code code;
            std::filesystem::remove_all(root, code);
        }
    };

    JBro::AssetId FindSpriteOf(JBro::EditorApplication& editor, const char* texturePath)
    {
        const JBro::AssetRegistry& registry = editor.GetAssetRegistry();
        const JBro::AssetRecord* texture = registry.FindByPath(texturePath);
        Check(texture != nullptr, "the probe texture must be registered");
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite && record.owner == texture->id)
            {
                return record.id;
            }
        }
        Check(false, "the probe texture must have a sprite");
        return {};
    }

    JBro::Component::SpriteRenderer2D* FindHeroSprite(JBro::EditorApplication& editor)
    {
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* hero = nullptr;
        canvas->ForEachObject([&hero](JBro::GameObject& object) { hero = &object; });
        Check(hero != nullptr, "the opened canvas must hold its object");
        return canvas->FindComponentRaw<JBro::Component::SpriteRenderer2D>(hero);
    }

    // 캔버스에 스프라이트 오브젝트 하나를 적어 두고 캔버스는 비운다. 여는 쪽이 빈 캔버스에만 읽는다.
    void WriteSpriteCanvas(JBro::EditorApplication& editor, const JBro::AssetId& sprite, const std::filesystem::path& path)
    {
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Hero");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(object) != nullptr, "the hero takes a transform");
        auto* renderer = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(renderer != nullptr, "the hero takes a sprite renderer");
        renderer->spriteId = sprite;
        JBro::CanvasFileError error;
        Check(editor.SaveCanvas(path.generic_string().c_str(), error), "the probe canvas must be written");
        Check(canvas->Clear(), "the canvas empties before the open");
    }

    // 여는 틱에는 캔버스가 서고 그림은 아직 없다. 로드가 끝난 틱에 붙는다. 로드가 잡던 참조는 놓인다.
    // 프로젝트를 다시 열면 보던 캔버스가 같은 길로 열리고, 파일이 망가졌으면 알림이 되며, 도는 중에 닫으면 취소된다.
    void TestACanvasOpensBeforeItsAssetsFinishLoading()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        // 워커 없이 엔진 틱마다 돈다. 로드가 끝나는 틱을 정확히 알 수 있다.
        config.taskWorkers = false;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; editor loading not verified" << std::endl;
            return;
        }
        LoadProbe probe;
        probe.Make("JBroEditorLoadingProbe");
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(probe.projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        const JBro::AssetId sprite = FindSpriteOf(editor, "Art/tiny.png");
        WriteSpriteCanvas(editor, sprite, probe.canvas);
        JBro::AssetSystem* assets = editor.GetAssetSystem();
        Check(assets != nullptr && assets->GetLoadedCount() == 0, "nothing is loaded before the canvas opens");

        editor.RequestOpenCanvas("Scenes/Opening.jcanvas");
        bool sawLoading = false;
        for (int frame = 0; frame < 20 && (frame == 0 || editor.IsCanvasLoading()); ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while the canvas loads");
            if (editor.IsCanvasLoading())
            {
                sawLoading = true;
                Check(editor.GetCanvas()->GetObjectCount() == 1, "the canvas stands before its assets are in");
                Check(FindHeroSprite(editor)->sprite.generation == 0, "and its sprite is not bound yet");
            }
        }
        Check(sawLoading, "the open must go through the async load, not bind in place");
        Check(false == editor.IsCanvasLoading(), "the load must finish");
        JBro::Component::SpriteRenderer2D* renderer = FindHeroSprite(editor);
        Check(renderer->sprite.generation != 0 && assets->GetSprite(renderer->sprite) != nullptr,
            "the sprite is bound once the load finished");
        const JBro::AssetHandle texture = assets->GetSprite(renderer->sprite)->texture;
        Check(assets->GetReferenceCount(texture) == 1, "the load let go of its hold - only the sprite holds the texture");

        // 다시 열면 보던 캔버스가 같은 길로 열린다. 이번에는 파일이 망가져 알림이 된다.
        editor.CloseProject();
        probe.WritePng(false);
        Check(editor.OpenProjectFile(probe.projectPath.c_str(), error), "the probe project opens again");
        Check(editor.IsCanvasLoading(), "opening the project starts loading the canvas from last time");
        const JBro::TaskGroupId broken = editor.GetCanvasLoadGroup();
        for (int frame = 0; frame < 20 && editor.IsCanvasLoading(); ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while the broken canvas loads");
        }
        // 상태 표시줄의 셈은 실패한 하위 작업도 끝난 것으로 센다 - 그러지 않으면 실패한 로드가 끝나지 않은 것처럼 보인다.
        const JBro::Widget::TaskGroupSummary brokenSummary = JBro::Widget::SummarizeTaskGroup(*editor.GetTaskManager(), broken);
        Check(brokenSummary.found && brokenSummary.done == 1 && brokenSummary.failed == 1 && brokenSummary.total == 1,
            "a failed sub-task counts as done and as failed");
        Check(brokenSummary.state == JBro::TaskState::Failed, "and the broken load reads Failed");
        Check(false == editor.IsCanvasLoading(), "the broken load must finish too");
        Check(FindHeroSprite(editor)->sprite.generation == 0, "a sprite whose image could not be read stays unbound");
        Check(std::strcmp(editor.GetNotifications().GetLastTitle(),
                  JBro::Loc::TextOr(JBro::LocKeys::NotifyCanvasAssetsFailedTitle, "Some assets could not be loaded")) == 0,
            "a failed load says so in a notification");

        // 도는 중에 닫으면 취소하고 기다린다. 워커가 없으니 아직 한 번도 돌지 않았다.
        editor.CloseProject();
        probe.WritePng(true);
        Check(editor.OpenProjectFile(probe.projectPath.c_str(), error), "the probe project opens a third time");
        Check(editor.IsCanvasLoading(), "the canvas from last time is loading");
        JBro::TaskManager* tasks = editor.GetTaskManager();
        const JBro::TaskGroupId group = editor.GetCanvasLoadGroup();
        Check(tasks != nullptr && tasks->FindGroup(group) != nullptr, "the load is a group the manager knows");
        editor.CloseProject();
        Check(false == editor.IsCanvasLoading(), "closing the project takes the load away");
        const JBro::TaskGroup* canceled = tasks->FindGroup(group);
        Check(canceled != nullptr && canceled->IsFinished() && canceled->GetState() == JBro::TaskState::Canceled,
            "the load was canceled and waited for, not left running");

        editor.Shutdown();
        probe.Remove();
    }

    // 하위 작업 하나를 알린 뒤 풀어 줄 때까지 기다린다. 상태 표시줄이 그동안 이 묶음을 보인다.
    class GateTask final : public JBro::Task
    {
    public:
        explicit GateTask(std::atomic<bool>& release)
            : Task("gate", 3)
            , m_release(release)
        {
        }

    protected:
        void Run() override
        {
            SucceedSubTask();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (false == m_release.load() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

    private:
        std::atomic<bool>& m_release;
    };

    // 상태 표시줄은 창 바닥 한 줄이고 도크는 그만큼 줄어든다. 도는 묶음을 누르면 그 위에 태스크 목록이 펼쳐지고,
    // 묶음이 끝나면 접힌다. 마지막 알림을 누르면 로그 창이 열린다.
    void TestTheStatusBarShowsRunningTasks()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        config.taskWorkerCount = 2;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; status bar not verified" << std::endl;
            return;
        }
        LoadProbe probe;
        probe.Make("JBroEditorStatusBarProbe");
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(probe.projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        Check(editor.Tick(Frame), "the editor must tick");
        Check(editor.Tick(Frame), "the editor must tick");

        const float displayHeight = ImGui::GetIO().DisplaySize.y;
        ImGuiWindow* bar = ImGui::FindWindowByName("##EditorStatusBar");
        ImGuiWindow* root = ImGui::FindWindowByName("##EditorRoot");
        Check(bar != nullptr && root != nullptr, "the status bar and the root dock are windows");
        std::cout << "  [measure] display " << displayHeight << " bar " << bar->Pos.y << "+" << bar->Size.y
                  << " root " << root->Pos.y << "+" << root->Size.y << std::endl;
        Check(std::fabs(bar->Pos.y + bar->Size.y - displayHeight) < 1.0f, "the status bar sits on the bottom edge");
        Check(std::fabs(root->Size.y + bar->Size.y - displayHeight) < 1.0f, "the dock gives up the status bar's line");
        Check(false == IsWindowActive("##EditorTaskList"), "no task list while nothing runs");

        std::atomic<bool> release = false;
        JBro::TaskManager* tasks = editor.GetTaskManager();
        Check(tasks != nullptr && tasks->UsesWorkers(), "the editor's task manager runs workers");
        JBro::OwnerPtr<JBro::TaskGroup> group = JBro::MakeOwnerPtr<JBro::TaskGroup>(JBro::String(JBro::LocKeys::TaskLoadCanvas));
        group->Add(JBro::MakeOwnerPtr<GateTask>(release));
        const JBro::TaskGroupId id = tasks->Submit(std::move(group));
        Check(editor.Tick(Frame), "the editor must tick with a running group");
        // 상태 표시줄이 읽는 셈이다. 하위 작업 셋 가운데 하나를 알리고 기다리는 중이다.
        for (int frame = 0; frame < 600 && JBro::Widget::SummarizeTaskGroup(*tasks, id).done == 0; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while the gate starts");
        }
        const JBro::Widget::TaskGroupSummary running = JBro::Widget::SummarizeTaskGroup(*tasks, id);
        Check(running.found && running.done == 1 && running.total == 3 && running.taskCount == 1,
            "the summary counts one of three sub-tasks done");
        Check(running.state == JBro::TaskState::Running, "and reads Running");
        Check(std::strcmp(running.nameKey, JBro::LocKeys::TaskLoadCanvas) == 0, "and carries the group's key");
        Check(false == JBro::Widget::SummarizeTaskGroup(*tasks, JBro::TaskGroupId{987654321}).found,
            "a group nobody knows is not found");

        const int barY = static_cast<int>(bar->Pos.y + bar->Size.y * 0.5f);
        ClickAt(editor, hwnd, 16, barY);
        Check(IsWindowActive("##EditorTaskList"), "clicking the running group opens the task list");
        ImGuiWindow* list = ImGui::FindWindowByName("##EditorTaskList");
        Check(list->Pos.y + list->Size.y <= bar->Pos.y + 1.0f, "the list opens above the status bar");

        release = true;
        for (int frame = 0; frame < 600 && false == tasks->FindGroup(id)->IsFinished(); ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while the group ends");
        }
        Check(tasks->FindGroup(id)->IsFinished(), "the gate group finishes");
        const JBro::Widget::TaskGroupSummary finished = JBro::Widget::SummarizeTaskGroup(*tasks, id);
        Check(finished.done == 3 && finished.state == JBro::TaskState::Completed,
            "a finished gate counts all three and reads Completed");
        Check(editor.Tick(Frame), "the editor must tick");
        Check(editor.Tick(Frame), "the editor must tick");
        Check(false == IsWindowActive("##EditorTaskList"), "the list folds away once nothing runs");

        JBro::EditorPanel* log = editor.FindPanel("Log");
        Check(log != nullptr, "the editor has a log panel");
        log->SetOpen(false);
        editor.GetNotifications().Notify(JBro::NotificationLevel::Warning, "status bar probe");
        Check(editor.Tick(Frame), "the editor must tick");
        // 알림 상자는 상태 표시줄 위에 선다. 그 줄을 덮지 않는다.
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->Active && std::strstr(window->Name, "##notification_") != nullptr)
            {
                Check(window->Pos.y + window->Size.y <= bar->Pos.y + 1.0f, "a notification box stays above the status bar");
            }
        }
        Check(std::strcmp(editor.GetNotifications().GetLastTitle(), "status bar probe") == 0, "the last notification is kept");
        ClickAt(editor, hwnd, static_cast<int>(ImGui::GetIO().DisplaySize.x) - 12, barY);
        Check(log->IsOpen(), "clicking the last notification on the status bar opens the log");

        editor.Shutdown();
        probe.Remove();
    }
}

int RunEditorLoadingTests()
{
    try
    {
        TestACanvasOpensBeforeItsAssetsFinishLoading();
        TestTheStatusBarShowsRunningTasks();
    }
    catch (const std::exception& error)
    {
        std::cout << "editor loading tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "editor loading tests passed" << std::endl;
    return 0;
}
