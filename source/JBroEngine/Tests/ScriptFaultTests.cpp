#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptGuard.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **스크립트가 죽어도 에디터는 산다**(D-325)를 적대적으로 본다. 시험이 등록한 스크립트를 에디터의 진짜 `ScriptSystem`·물리 경로로 돌리며
// 훅마다·방식마다 일부러 터트린다. 시나리오마다 다음을 본다: 실행이 멈춘다, 에디터 프레임이 계속 돈다, 캔버스가 실행 전 글자와
// 한 글자도 다르지 않게 돌아온다, 오류 알림과 로그가 남는다, 다음 실행에서 훅이 다시 돈다. 같은 에디터·같은 캔버스로 시나리오를
// 두 바퀴 돌려 되돌리기가 쌓여도 어긋나지 않는지도 본다.

namespace
{
    using namespace JBro;

    constexpr Float Frame = 1.0f / 60.0f;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    enum class FaultMode
    {
        None,
        NullInCreate,
        ThrowInStart,
        NullInUpdate,
        DivideInUpdate,
        RecurseInUpdate,
        BreakInUpdate,
        NullInFixedUpdate,
        NullInCollision,
        WreckThenNullInUpdate,
        DestroyScriptedThenNull,
        NullInAttached
    };

    // 시험이 스크립트에게 시키는 일과 스크립트가 센 것이다. 스크립트는 이 실행 파일 안에 있으므로 전역으로 주고받는다.
    struct FaultControl
    {
        FaultMode mode = FaultMode::None;
        Int32 triggerUpdate = 1;
        Int32 creates = 0;
        Int32 updates = 0;
        Int32 fixedUpdates = 0;
        Int32 collisions = 0;
        Int32 faultsRaised = 0;
        Canvas* canvas = nullptr;
    };

    FaultControl g_control;

    __declspec(noinline) void WriteThroughNull()
    {
        ++g_control.faultsRaised;
        volatile int* nothing = nullptr;
        *nothing = 1;
    }

#pragma warning(push)
#pragma warning(disable : 4717)
    __declspec(noinline) int RecurseForever(int depth)
    {
        volatile char pad[256];
        pad[0] = static_cast<char>(depth);
        return RecurseForever(depth + 1) + pad[0];
    }
#pragma warning(pop)

    // 오브젝트 둘에 붙는다. 첫 스크립트가 터지면 같은 프레임의 둘째 스크립트 훅은 불리지 않아야 한다 - 예외는 하나만 난다.
    class FaultScript final : public GameScript2D
    {
        JBRO_REFLECT_BODY(FaultScript)
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::FaultScript";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 실행 중에 바꾸는 필드다. 되돌린 캔버스에서 처음 값이어야 한다.
        JBRO_FIELD(Float, speed) = 1.0f;

        void OnAttached() override
        {
            if (g_control.mode == FaultMode::NullInAttached)
            {
                WriteThroughNull();
            }
        }

        void OnCreate() override
        {
            ++g_control.creates;
            if (g_control.mode == FaultMode::NullInCreate)
            {
                WriteThroughNull();
            }
        }

        void OnStart() override
        {
            if (g_control.mode == FaultMode::ThrowInStart)
            {
                ++g_control.faultsRaised;
                throw std::runtime_error("a script threw in OnStart");
            }
        }

        void OnUpdate() override
        {
            ++g_control.updates;
            speed = speed + 1.0f;
            // 스크립트가 붙은 오브젝트를 지운 지 몇 프레임 뒤, 남은 스크립트가 터진다.
            if (g_control.mode == FaultMode::DestroyScriptedThenNull && g_control.updates == g_control.triggerUpdate + 3)
            {
                WriteThroughNull();
                return;
            }
            if (g_control.updates != g_control.triggerUpdate)
            {
                return;
            }
            switch (g_control.mode)
            {
            case FaultMode::NullInUpdate:
                WriteThroughNull();
                break;
            case FaultMode::DivideInUpdate:
            {
                ++g_control.faultsRaised;
                volatile int zero = 0;
                volatile int result = 7 / zero;
                (void)result;
                break;
            }
            case FaultMode::RecurseInUpdate:
                ++g_control.faultsRaised;
                RecurseForever(0);
                break;
            case FaultMode::BreakInUpdate:
                ++g_control.faultsRaised;
                __debugbreak();
                break;
            case FaultMode::WreckThenNullInUpdate:
            {
                // 게임이 캔버스를 마구 바꾼 뒤 죽는다: 오브젝트를 만들고, 남의 오브젝트를 지우고, 값을 바꾼다.
                Canvas& canvas = *g_control.canvas;
                for (Int32 index = 0; index < 20; ++index)
                {
                    Object::GameObject* spawned = canvas.CreateObject("Spawned");
                    canvas.AttachComponent<Component::Transform2D>(spawned)->position = Vector2{Float(static_cast<float>(index.Value)), 9.0f};
                }
                Array<Object::GameObject*> roots;
                canvas.GetRootObjects(roots);
                for (Object::GameObject* root : roots)
                {
                    if (std::strcmp(root->GetName(), "Ground") == 0)
                    {
                        canvas.DestroyObject(root);
                    }
                    else if (auto* transform = canvas.FindComponentRaw<Component::Transform2D>(root))
                    {
                        transform->position = Vector2{-99.0f, -99.0f};
                    }
                }
                WriteThroughNull();
                break;
            }
            case FaultMode::DestroyScriptedThenNull:
            {
                // 스크립트가 붙은 오브젝트(`Kept`)를 지운다. 되돌린 캔버스에는 그 오브젝트와 스크립트가 다시 있어야 한다.
                Array<Object::GameObject*> roots;
                g_control.canvas->GetRootObjects(roots);
                for (Object::GameObject* root : roots)
                {
                    if (std::strcmp(root->GetName(), "Kept") == 0)
                    {
                        g_control.canvas->DestroyObject(root);
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        void OnFixedUpdate() override
        {
            ++g_control.fixedUpdates;
            if (g_control.mode == FaultMode::NullInFixedUpdate && g_control.fixedUpdates == g_control.triggerUpdate)
            {
                WriteThroughNull();
            }
        }

        void OnCollisionEnter(const Collision2D&) override
        {
            ++g_control.collisions;
            if (g_control.mode == FaultMode::NullInCollision)
            {
                WriteThroughNull();
            }
        }

    };

    struct Scenario
    {
        const char* name;
        FaultMode mode;
        Int32 triggerUpdate;
        UInt32 code;
        // 일시정지 뒤 한 프레임 진행으로 들어가 터진다.
        Bool stepWhilePaused;
    };

    String CanvasText(Canvas& canvas)
    {
        String text;
        CanvasFileError error;
        Check(WriteCanvasText(canvas, text, error), "the canvas must be writable as text");
        return text;
    }

    // 그 글자가 든 로그 줄이 있는가.
    Bool Logged(const char* needle)
    {
        const std::size_t count = Log::GetCount();
        for (std::size_t index = 0; index < count; ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry != nullptr && std::strstr(entry->message, needle) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    // 다음 실행에서 훅이 다시 돌고, 정지가 캔버스를 그대로 되돌린다. 예외 뒤에 보호가 막힌 채 남으면 여기서 드러난다.
    void PlayCleanly(EditorApplication& editor, Canvas& canvas, const String& baseline)
    {
        g_control.mode = FaultMode::None;
        g_control.updates = 0;
        Check(editor.StartSimulation(), "play must start again after a fault");
        for (Int32 frame = 0; frame < 6; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while playing cleanly");
        }
        Check(editor.IsSimulationPlaying(), "a clean play keeps running");
        Check(g_control.updates >= 6, "and the script hooks run again - the guard did not stay closed");
        editor.StopSimulation();
        Check(editor.Tick(Frame), "the editor must tick after stopping");
        Check(CanvasText(canvas) == baseline, "a clean stop restores the canvas exactly");
    }

    void RunPlayScenario(EditorApplication& editor, Canvas& canvas, const String& baseline, const Scenario& scenario)
    {
        std::cout << "  scenario: " << scenario.name << std::endl;
        g_control.mode = scenario.mode;
        g_control.triggerUpdate = scenario.triggerUpdate;
        g_control.creates = 0;
        g_control.updates = 0;
        g_control.fixedUpdates = 0;
        g_control.collisions = 0;
        g_control.faultsRaised = 0;
        // 로그는 고리라 찬 뒤에는 개수로 "이 뒤" 를 가를 수 없다. 시나리오마다 비운다.
        Log::Clear();

        Check(editor.StartSimulation(), "play must start");
        if (scenario.stepWhilePaused)
        {
            editor.SetSimulationPaused(true);
            Check(editor.Tick(Frame), "the editor must tick while paused");
            Check(g_control.updates == 0, "a paused simulation runs no script");
            editor.StepSimulation();
        }
        Int32 frames = 0;
        while (editor.IsSimulationPlaying() && frames < 240)
        {
            Check(editor.Tick(Frame), "the editor must keep ticking through a script fault");
            ++frames;
        }
        Check(false == editor.IsSimulationPlaying(), "the fault must stop the simulation");
        Check(g_control.faultsRaised == 1, "exactly one fault is raised - no hook runs after the first one");
        Check(false == ScriptGuard::HasFault(), "the editor took the fault");
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(Frame), "the editor stays alive after the fault");
        }
        Check(CanvasText(canvas) == baseline, "the canvas is back exactly as it was before play");
        Check(editor.GetSelectedObject() == nullptr, "no selection points into the thrown away canvas");
        Check(editor.GetNotifications().GetLastLevel() == NotificationLevel::Error, "the user is told with an error");
        Check(std::strcmp(editor.GetNotifications().GetLastTitle(),
                  Loc::TextOr(LocKeys::NotifyScriptFaultStopped, "A script error stopped the simulation")) == 0,
            "that says the simulation stopped");
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned int>(scenario.code.Value));
        Check(Logged(code), "the log names the exception code");

        PlayCleanly(editor, canvas, baseline);
    }

    void TestScriptFaultsNeverTakeTheEditorDown()
    {
        RegisterScriptType<FaultScript>();
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; script faults not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "ScriptFaultProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        Canvas& canvas = *editor.GetCanvas();
        g_control = FaultControl{};
        g_control.canvas = &canvas;

        // 스크립트 둘·고정 바닥·바닥에 겹친 채 떨어지는 상자. 상자의 스크립트가 첫 고정 스텝에서 충돌을 받는다.
        Object::GameObject* kept = canvas.CreateObject("Kept");
        canvas.AttachComponent<Component::Transform2D>(kept)->position = Vector2{3.0f, 4.0f};
        Check(canvas.AttachScript(kept, FaultScript::StaticTypeName()) != nullptr, "the fault script attaches by name");
        Object::GameObject* ground = canvas.CreateObject("Ground");
        canvas.AttachComponent<Component::Transform2D>(ground)->position = Vector2{0.0f, -0.5f};
        canvas.AttachComponent<Component::Collider2D>(ground)->size = Vector2{40.0f, 1.0f};
        Object::GameObject* box = canvas.CreateObject("Box");
        canvas.AttachComponent<Component::Transform2D>(box)->position = Vector2{0.0f, 0.3f};
        canvas.AttachComponent<Component::Collider2D>(box);
        canvas.AttachComponent<Component::Rigidbody2D>(box);
        Check(canvas.AttachScript(box, FaultScript::StaticTypeName()) != nullptr, "and a second one rides on the box");
        Check(editor.Tick(Frame), "the editor must tick before play");
        const String baseline = CanvasText(canvas);

        // 무엇이 확실히 되는지부터: 깨끗한 실행에서 충돌 훅이 실제로 불린다. 아니면 충돌 시나리오가 아무것도 보지 않는다.
        PlayCleanly(editor, canvas, baseline);
        Check(g_control.collisions > 0, "the box's script hears the collision during a clean play");

        const Scenario scenarios[] = {
            {"null write in OnCreate", FaultMode::NullInCreate, 1, 0xC0000005u, false},
            {"C++ exception in OnStart", FaultMode::ThrowInStart, 1, 0xE06D7363u, false},
            {"null write in OnUpdate", FaultMode::NullInUpdate, 30, 0xC0000005u, false},
            {"integer divide by zero in OnUpdate", FaultMode::DivideInUpdate, 12, 0xC0000094u, false},
            {"endless recursion in OnUpdate", FaultMode::RecurseInUpdate, 8, 0xC00000FDu, false},
            {"__debugbreak in OnUpdate with no debugger", FaultMode::BreakInUpdate, 5, 0x80000003u, false},
            {"null write in OnFixedUpdate", FaultMode::NullInFixedUpdate, 3, 0xC0000005u, false},
            {"null write in OnCollisionEnter", FaultMode::NullInCollision, 1, 0xC0000005u, false},
            {"spawn, delete and move, then null write", FaultMode::WreckThenNullInUpdate, 10, 0xC0000005u, false},
            {"destroy a scripted object, then null write", FaultMode::DestroyScriptedThenNull, 4, 0xC0000005u, false},
            {"null write in the first frame stepped while paused", FaultMode::NullInUpdate, 1, 0xC0000005u, true},
        };
        // 두 바퀴: 되돌리기가 쌓여도 캔버스가 같은 글자로 돌아오고, 보호가 매번 다시 열린다.
        for (Int32 round = 0; round < 2; ++round)
        {
            for (const Scenario& scenario : scenarios)
            {
                RunPlayScenario(editor, canvas, baseline, scenario);
            }
        }

        // 편집 중(실행 아님)에 붙인 스크립트가 붙자마자 죽는다. 멈출 것이 없고 알리기만 하며, 에디터와 캔버스는 산다.
        {
            std::cout << "  scenario: null write in OnAttached while editing" << std::endl;
            g_control.mode = FaultMode::NullInAttached;
            g_control.faultsRaised = 0;
            const std::size_t objectsBefore = canvas.GetObjectCount();
            Object::GameObject* late = canvas.CreateObject("Late");
            canvas.AttachScript(late, FaultScript::StaticTypeName());
            Check(g_control.faultsRaised == 1, "the attach hook faulted");
            Check(editor.Tick(Frame), "the editor keeps ticking");
            Check(false == ScriptGuard::HasFault(), "and takes the fault");
            Check(std::strcmp(editor.GetNotifications().GetLastTitle(),
                      Loc::TextOr(LocKeys::NotifyScriptFault, "A script raised an error")) == 0,
                "saying a script raised an error, with nothing to stop");
            Check(canvas.GetObjectCount() == objectsBefore + 1, "the canvas keeps what the user made");
            canvas.DestroyObject(late);
            canvas.FlushPendingDestroy();
            g_control.mode = FaultMode::None;
            Check(editor.Tick(Frame), "the editor keeps ticking after the cleanup");
            Check(CanvasText(canvas) == baseline, "and the canvas is as it was");
        }

        editor.Shutdown();
        Check(false == ScriptGuard::IsEnabled(), "shutting the editor down turns the guard off");
    }
}

JBro::Int32 RunScriptFaultTests()
{
    TestScriptFaultsNeverTakeTheEditorDown();
    std::cout << "ScriptFault tests passed.\n";
    return 0;
}
