#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Input/InputSystem.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/Array.h>

#include <crtdbg.h>

#include <atomic>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 입력 레이어 체인과 블로킹을 잰다(D-214, input-plan §4 의 3).
namespace
{
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    std::atomic<int> g_crtAllocations{0};
    int CountCrtAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            g_crtAllocations.fetch_add(1, std::memory_order_relaxed);
        }
        return 1;
    }
#endif

    Array<int> g_dispatchLog;

    // 핸들러 프로브의 몸이다. 레이어·순서는 아래 파생 타입의 상속 줄이 정한다.
    class HandlerProbe : public GameScript2D
    {
    public:
        InputResult Handle(InputView& input)
        {
            g_dispatchLog.Add(mark);
            sawW = input.Keyboard().IsDown(Key::W);
            sawMouse = input.Mouse().IsDown(MouseButton::Left);
            if (takeMouse)
            {
                input.Consume(InputDevice::Mouse);
                // 소비는 아래로 내려가는 것이다. 가져간 핸들러 자신은 계속 읽는다.
                sawMouseAfterConsume = input.Mouse().IsDown(MouseButton::Left);
            }
            if (disableTarget != nullptr)
            {
                disableTarget->SetEnabled(false);
            }
            return block ? InputResult::Block : InputResult::Pass;
        }

        int mark = 0;
        bool block = false;
        bool takeMouse = false;
        GameScriptBase* disableTarget = nullptr;
        bool sawW = false;
        bool sawMouse = false;
        bool sawMouseAfterConsume = false;
    };

#define JBRO_TEST_HANDLER(Name, Layer, Order)                                        \
    class Name final : public HandlerProbe, public InputHandler<Layer, Order>        \
    {                                                                                \
    public:                                                                          \
        static constexpr const char* StaticTypeName()                                \
        {                                                                            \
            return "InputChainTests::" #Name;                                        \
        }                                                                            \
        ComponentTypeId GetTypeId() const override                                   \
        {                                                                            \
            return MakeStableTypeId(StaticTypeName());                               \
        }                                                                            \
        InputResult OnInput(InputView& input) override                               \
        {                                                                            \
            return Handle(input);                                                    \
        }                                                                            \
    }

    JBRO_TEST_HANDLER(ModalProbe, "Modal", 0);
    JBRO_TEST_HANDLER(UiHighProbe, "UI", 10);
    JBRO_TEST_HANDLER(UiProbe, "UI", 0);
    JBRO_TEST_HANDLER(GameProbe, "Game", 0);
    JBRO_TEST_HANDLER(NowhereProbe, "Nowhere", 0);
    // 이름으로 붙이는(DLL 과 같은 길의) 핸들러다. 스크립트 풀의 타입 표가 썽크를 든다.
    JBRO_TEST_HANDLER(NamedProbe, "Game", 0);

#undef JBRO_TEST_HANDLER

    // 핸들러가 아닌 스크립트다. `OnUpdate`·`OnFixedUpdate` 에서 폴링만 한다.
    class PollingProbe final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "InputChainTests::PollingProbe";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate() override
        {
            sawW = GetInputServices().Input.Keyboard().IsDown(Key::W);
            sawMouse = GetInputServices().Input.Mouse().IsDown(MouseButton::Left);
        }

        void OnFixedUpdate() override
        {
            ++fixedSteps;
            fixedSawW = GetInputServices().Input.Keyboard().IsDown(Key::W);
        }

        bool sawW = false;
        bool sawMouse = false;
        int fixedSteps = 0;
        bool fixedSawW = false;
    };

    InputEvent Held(Key key)
    {
        InputEvent event;
        event.kind = InputEventKind::KeyDown;
        event.key = key;
        return event;
    }

    InputEvent HeldButton(MouseButton button)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseButtonDown;
        event.button = button;
        return event;
    }

    // 캔버스 하나, 스크립트 시스템 하나, 입력 시스템 하나다. 프레임은 호스트와 같은 차례로 돈다:
    // 입력 접기 → 체인 → 스크립트 갱신.
    struct Rig
    {
        Canvas canvas{CreateDefaultAllocator()};
        System::ScriptSystem scripts;
        System::InputSystem input;

        Rig()
        {
            scripts.SetInputSystem(&input);
            scripts.Initialize(canvas);
            BindInputSystemContext(input.GetSystemContext());
            BindInputServiceContext(input.GetServiceContext());
        }

        ~Rig()
        {
            scripts.Shutdown(canvas);
            BindInputSystemContext({});
            BindInputServiceContext({});
        }

        template<typename T>
        T* Add(int mark)
        {
            Object::GameObject* object = canvas.CreateObject("probe");
            T* script = canvas.AttachScript<T>(object);
            script->mark = mark;
            return script;
        }

        void Frame(JArrayView<InputEvent> events)
        {
            input.BeginFrame(events);
            scripts.DispatchInput(canvas);
            scripts.Update(canvas, 1.0f / 60.0f);
        }

        // 붙인 스크립트가 시작 훅을 받게 한 프레임 돌린다. 이 프레임의 체인에는 아직 서지 않는다.
        void Start()
        {
            Frame({});
            g_dispatchLog.Clear();
        }
    };

    const InputEvent g_heldWAndMouse[] = { Held(Key::W), HeldButton(MouseButton::Left) };
    const JArrayView<InputEvent> HeldWAndMouse = { g_heldWAndMouse, 2 };

    std::size_t CountUnknownLayerWarnings()
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < Log::GetCount(); ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry != nullptr && std::strstr(entry->message, "unknown input layer") != nullptr)
            {
                ++count;
            }
        }
        return count;
    }

    // 레이어 순위(프로젝트 순서) → 같은 레이어에서 `Order` 큰 것 → 실행 순서. 없는 레이어는 맨 아래.
    void TestHandlersRunInLayerThenOrderThenExecutionOrder()
    {
        Log::Clear();
        Rig rig;
        rig.Add<NowhereProbe>(6);
        rig.Add<GameProbe>(4);
        rig.Add<UiProbe>(3);
        rig.Add<ModalProbe>(1);
        rig.Add<GameProbe>(5);
        rig.Add<UiHighProbe>(2);
        rig.Start();
        Check(rig.scripts.GetInputHandlerCount() == 6, "every handler script must stand in the chain");

        rig.Frame(HeldWAndMouse);
        Check(g_dispatchLog.Size() == 6, "every handler is called once per frame");
        for (std::size_t index = 0; index < 6; ++index)
        {
            Check(g_dispatchLog[index] == static_cast<int>(index) + 1,
                "handlers must run by layer, then by larger order, then by execution order");
        }
        Check(CountUnknownLayerWarnings() == 1, "an unknown layer is reported once");

        rig.Frame({});
        Check(g_dispatchLog.Size() == 12, "handlers are called even on a frame without input");
        Check(CountUnknownLayerWarnings() == 1, "and the unknown layer is not reported again");

        // 스크립트가 하나 더 붙으면 체인을 다시 세운다. 그때도 같은 레이어를 두 번 말하지 않는다.
        rig.Add<GameProbe>(9);
        rig.Frame({});
        Check(rig.scripts.GetInputHandlerCount() == 7, "a new handler joins when the chain is rebuilt");
        Check(CountUnknownLayerWarnings() == 1, "rebuilding the chain does not repeat the warning");
    }

    // 시작 훅을 받기 전에는 체인에 서지 않는다. 그 프레임에 붙인 것은 다음 프레임부터다(D-214 (6)).
    void TestAScriptJoinsTheChainAfterItStarts()
    {
        Rig rig;
        rig.Start();
        GameProbe* late = rig.Add<GameProbe>(7);
        rig.Frame(HeldWAndMouse);
        Check(g_dispatchLog.IsEmpty(), "a script that has not started does not receive input");
        rig.Frame(HeldWAndMouse);
        Check(g_dispatchLog.Size() == 1 && late->sawW, "once started it receives input");
    }

    // 기존 엔진의 반환값 블로킹이다. 모달이 막으면 아래 핸들러도 폴링도 아무것도 받지 않는다.
    void TestBlockStopsEveryLowerHandlerAndThePolling()
    {
        Rig rig;
        ModalProbe* modal = rig.Add<ModalProbe>(1);
        GameProbe* game = rig.Add<GameProbe>(2);
        PollingProbe* poller = rig.canvas.AttachScript<PollingProbe>(rig.canvas.CreateObject("poller"));
        rig.Start();

        modal->block = true;
        rig.Frame(HeldWAndMouse);
        Check(g_dispatchLog.Size() == 1 && g_dispatchLog[0] == 1, "nothing below a blocking handler is called");
        Check(modal->sawW, "the blocking handler itself saw the input");
        Check(false == poller->sawW && false == poller->sawMouse, "polling below a block sees nothing");
        Check(GetInputServices().Input.GetView().IsConsumed(InputDevice::Keyboard),
            "the residual view says the keyboard was taken");

        modal->block = false;
        g_dispatchLog.Clear();
        rig.Frame({});
        Check(g_dispatchLog.Size() == 2 && game->sawW, "once the block lifts the lower handler receives input again");
        Check(poller->sawW && poller->sawMouse, "and so does polling");
    }

    // 장치 단위 소비다. 마우스만 가져가면 아래는 키보드를 그대로 받는다(P2: 인벤토리 창이 WASD 까지 막던 문제).
    void TestConsumingOneDeviceLeavesTheOthers()
    {
        Rig rig;
        UiProbe* ui = rig.Add<UiProbe>(1);
        GameProbe* game = rig.Add<GameProbe>(2);
        PollingProbe* poller = rig.canvas.AttachScript<PollingProbe>(rig.canvas.CreateObject("poller"));
        rig.Start();

        ui->takeMouse = true;
        rig.Frame(HeldWAndMouse);
        Check(ui->sawMouse && ui->sawMouseAfterConsume, "the handler that took the mouse still reads it");
        Check(game->sawW, "the lower handler still receives the keyboard");
        Check(false == game->sawMouse, "but not the mouse that was taken");
        Check(poller->sawW && false == poller->sawMouse, "polling sees the same split");

        // 소비는 한 프레임이다. 다음 프레임에 가져가지 않으면 아래가 다시 받는다.
        ui->takeMouse = false;
        rig.Frame({});
        Check(game->sawMouse, "a consumed device returns on the next frame");
    }

    // 꺼진 스크립트는 부르지 않는다. 앞의 핸들러가 뒤의 것을 끄면 그 프레임에도 부르지 않는다.
    void TestDisabledHandlersAreSkipped()
    {
        Rig rig;
        ModalProbe* modal = rig.Add<ModalProbe>(1);
        GameProbe* first = rig.Add<GameProbe>(2);
        GameProbe* second = rig.Add<GameProbe>(3);
        rig.Start();

        first->SetEnabled(false);
        rig.Frame({});
        Check(g_dispatchLog.Size() == 2 && g_dispatchLog[1] == 3, "a disabled handler is not called");

        g_dispatchLog.Clear();
        modal->disableTarget = second;
        rig.Frame({});
        Check(g_dispatchLog.Size() == 1, "a handler switched off above in the same frame is not called");
    }

    // 프로젝트가 레이어 순서를 바꾸면 체인이 다시 줄 선다.
    void TestChangingTheLayerOrderResortsTheChain()
    {
        Rig rig;
        rig.Add<ModalProbe>(1);
        rig.Add<GameProbe>(2);
        rig.Start();

        const NameId gameFirst[] = { MakeNameId("Game"), MakeNameId("Modal") };
        rig.input.SetLayerOrder({gameFirst, 2});
        rig.Frame({});
        Check(g_dispatchLog.Size() == 2 && g_dispatchLog[0] == 2 && g_dispatchLog[1] == 1,
            "a new layer order must apply on the next dispatch");
    }

    // 이름으로 붙인 스크립트(스크립트 DLL 의 길)도 같은 체인에 선다. 썽크는 `ScriptTypeInfo` 가 든다.
    void TestNamedScriptsJoinTheChainThroughTheirTypeInfo()
    {
        Check(RegisterScriptType<NamedProbe>(), "the named probe must register");
        Rig rig;
        GameScriptBase* attached = rig.canvas.AttachScript(rig.canvas.CreateObject("named"), NamedProbe::StaticTypeName());
        Check(attached != nullptr, "the named probe must attach by name");
        static_cast<NamedProbe*>(attached)->mark = 8;
        rig.Start();
        rig.Frame(HeldWAndMouse);
        Check(g_dispatchLog.Size() == 1 && g_dispatchLog[0] == 8, "a script attached by name is a handler too");
        Check(static_cast<NamedProbe*>(attached)->sawW, "and it reads the input");
    }

    // 체인이 돌지 않은 프레임(멈춤, 스크립트가 없는 프레임워크)의 폴링은 이번 프레임 전체를 본다.
    // 지난 프레임의 블로킹이 남으면 아무도 막지 않았는데 입력이 사라진다.
    void TestAFrameWithoutTheChainIsNotBlocked()
    {
        struct Blocker final : IInputHandler
        {
            InputResult OnInput(InputView&) override
            {
                return InputResult::Block;
            }
        };
        System::InputSystem input;
        Blocker blocker;
        input.BeginFrame(HeldWAndMouse);
        input.BeginDispatch();
        Check(input.Deliver(blocker), "a blocking handler stops the chain");
        input.EndDispatch();
        Check(false == input.GetResidualView().Keyboard().IsDown(Key::W), "the block reaches the polling");

        input.BeginFrame({});
        Check(input.GetResidualView().Keyboard().IsDown(Key::W),
            "a frame without a chain must not keep the last frame's block");
    }

    // 매 프레임 도는 자리다. 체인을 도는 것이 힙을 건드리지 않는다(§9).
    void TestDispatchDoesNotAllocate()
    {
#if defined(_MSC_VER) && defined(_DEBUG)
        Rig rig;
        rig.Add<ModalProbe>(1);
        UiProbe* ui = rig.Add<UiProbe>(2);
        ui->takeMouse = true;
        rig.Add<GameProbe>(3);
        rig.Start();
        rig.Frame(HeldWAndMouse);
        g_dispatchLog.Reserve(1024);
        g_dispatchLog.Clear();

        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
        for (int frame = 0; frame < 200; ++frame)
        {
            rig.input.BeginFrame(HeldWAndMouse);
            rig.scripts.DispatchInput(rig.canvas);
            if (g_dispatchLog.Size() > 900)
            {
                g_dispatchLog.Clear();
            }
        }
        _CrtSetAllocHook(previous);
        Check(g_crtAllocations.load() == 0, "dispatching the input chain does not touch the CRT heap");
#endif
    }

    // 프레임워크가 체인을 고정 스텝보다 먼저 돌린다(D-214 (6)). 그러지 않으면 `OnFixedUpdate` 의 폴링이
    // 막히기 전의 입력을 본다.
    void TestTheFrameworkDispatchesBeforeTheFixedSteps()
    {
        System::InputSystem input;
        BindInputSystemContext(input.GetSystemContext());
        BindInputServiceContext(input.GetServiceContext());

        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.input = &input;
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        auto* modal = canvas->AttachScript<ModalProbe>(canvas->CreateObject("modal"));
        auto* poller = canvas->AttachScript<PollingProbe>(canvas->CreateObject("poller"));
        modal->mark = 1;
        modal->block = true;

        input.BeginFrame({});
        JBro::Testing::Tick(framework, 1.0f / 60.0f);
        g_dispatchLog.Clear();

        input.BeginFrame(HeldWAndMouse);
        JBro::Testing::Tick(framework, 1.0f / 60.0f);
        Check(g_dispatchLog.Size() == 1, "the framework must run the input chain every frame");
        Check(poller->fixedSteps > 0, "the frame must include a fixed step");
        Check(false == poller->fixedSawW, "polling in OnFixedUpdate must already see the block");
        Check(false == poller->sawW, "and so must polling in OnUpdate");

        framework.Shutdown();
        BindInputSystemContext({});
        BindInputServiceContext({});
    }

    // 버튼의 훅을 센다(D-237).
    class ButtonProbe final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "InputChainTests::ButtonProbe";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnPointerEnter() override
        {
            ++enters;
        }

        void OnPointerExit() override
        {
            ++exits;
        }

        void OnPointerDown() override
        {
            ++downs;
        }

        void OnPointerUp() override
        {
            ++ups;
        }

        void OnClick() override
        {
            ++clicks;
        }

        int enters = 0;
        int exits = 0;
        int downs = 0;
        int ups = 0;
        int clicks = 0;
    };

    InputEvent MouseAt(float x, float y)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseMove;
        event.x = x;
        event.y = y;
        return event;
    }

    InputEvent Released(MouseButton button)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseButtonUp;
        event.button = button;
        return event;
    }

    InputEvent Touch(InputEventKind kind, float x, float y)
    {
        InputEvent event;
        event.kind = kind;
        event.x = x;
        event.y = y;
        event.codePoint = 7;
        return event;
    }

    // **버튼은 `"UI"` 레이어에서 포인터를 가져간다**(D-237, ui-plan 3 단계). 200 x 100 게임 화면 = 기준이라 화면 레이어의 1 유닛이 1 픽셀이고,
    // 월드 카메라(세로 절반 50)도 1 유닛이 1 픽셀이다. 화면 버튼은 가운데 40 x 20, 월드 버튼은 (60, 0) 의 20 x 20 이고, 가운데 밑에는 월드 버튼이
    // 하나 더 깔려 있다(화면 레이어가 위다).
    void TestButtonsTakeThePointerOnTheUiLayer()
    {
        System::InputSystem input;
        BindInputSystemContext(input.GetSystemContext());
        BindInputServiceContext(input.GetServiceContext());
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.input = &input;
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Check(framework.BindScriptContexts(), "the script contexts bind");
        ScreenSpaceFrame frame;
        frame.referenceWidth = 200.0f;
        frame.referenceHeight = 100.0f;
        frame.targetWidth = 200.0f;
        frame.targetHeight = 100.0f;
        framework.SetScreenSpace(frame);
        Canvas* canvas = framework.GetCanvas();

        Object::GameObject* eye = canvas->CreateObject("eye");
        canvas->AttachComponent<Component::Transform2D>(eye);
        auto* camera = canvas->AttachComponent<Component::Camera2D>(eye);
        camera->primary = true;
        camera->orthographicSize = 50.0f;

        Layer& hud = canvas->CreateLayer("HUD");
        hud.SetSpace(LayerSpace::Screen);
        const auto makeButton = [&](const char* name, Layer* layer, Vector2 position, Vector2 size) {
            Object::GameObject* object = canvas->CreateObject(name);
            if (layer != nullptr)
            {
                canvas->SetObjectLayer(object, layer->GetId());
            }
            canvas->AttachComponent<Component::Transform2D>(object)->position = position;
            canvas->AttachComponent<Component::Button2D>(object)->size = size;
            canvas->AttachComponent<Component::SpriteRenderer2D>(object);
            return object;
        };
        Object::GameObject* play = makeButton("play", &hud, {0.0f, 0.0f}, {40.0f, 20.0f});
        Object::GameObject* sign = makeButton("sign", nullptr, {60.0f, 0.0f}, {20.0f, 20.0f});
        Object::GameObject* under = makeButton("under", nullptr, {0.0f, 0.0f}, {30.0f, 30.0f});
        auto* playButton = canvas->FindComponentRaw<Component::Button2D>(play);
        auto* playSprite = canvas->FindComponentRaw<Component::SpriteRenderer2D>(play);
        auto* probe = canvas->AttachScript<ButtonProbe>(play);
        auto* signProbe = canvas->AttachScript<ButtonProbe>(sign);
        auto* underProbe = canvas->AttachScript<ButtonProbe>(under);
        auto* poller = canvas->AttachScript<PollingProbe>(canvas->CreateObject("poller"));
        const Service::Screen2DService& screen = GetFramework2DServices().Screen2D;

        const auto frameWith = [&](std::initializer_list<InputEvent> events) {
            Array<InputEvent> list;
            for (const InputEvent& event : events)
            {
                list.Add(event);
            }
            input.BeginFrame({list.Data(), static_cast<std::uint32_t>(list.Size())});
            JBro::Testing::Tick(framework, 1.0f / 60.0f);
        };
        frameWith({});
        frameWith({});

        // 가운데로 오면 화면 버튼의 호버다. 밑의 월드 버튼은 받지 않는다.
        frameWith({MouseAt(100.0f, 50.0f)});
        Check(playButton->hovered && probe->enters == 1 && underProbe->enters == 0, "the pointer over the screen button hovers it, not the world one under it");
        Check(screen.IsPointerOverButton(), "and the service says the pointer is over a button");
        Check(playSprite->tint.R == playButton->hoverTint.R, "the hovered button takes its hover tint");

        // 누르면 눌림이고 게임의 폴링은 마우스를 보지 못한다.
        frameWith({HeldButton(MouseButton::Left)});
        Check(playButton->pressed && probe->downs == 1 && false == poller->sawMouse, "pressing the button hides the mouse from the game below");
        Check(playSprite->tint.R == playButton->pressedTint.R, "and it takes the pressed tint");
        frameWith({Released(MouseButton::Left)});
        Check(playButton->clicked && probe->ups == 1 && probe->clicks == 1 && underProbe->downs == 0, "releasing on the button clicks it once");
        frameWith({});
        Check(false == playButton->clicked, "the click lasts one frame");

        // 눌렀다가 벗어나 떼면 떼기만 있고 누름은 없다.
        frameWith({HeldButton(MouseButton::Left)});
        frameWith({MouseAt(190.0f, 90.0f)});
        Check(playButton->pressed && screen.IsPointerOverButton(), "a held button keeps the pointer while it is dragged away");
        frameWith({Released(MouseButton::Left)});
        Check(probe->ups == 2 && probe->clicks == 1 && probe->exits == 1, "releasing away from it is no click");

        // 눌렀다가 다른 버튼 위에서 떼면 어느 쪽도 누름이 아니다(월드 (60, 0) 버튼은 픽셀 (160, 50)).
        frameWith({MouseAt(100.0f, 50.0f), HeldButton(MouseButton::Left)});
        frameWith({MouseAt(160.0f, 50.0f)});
        frameWith({Released(MouseButton::Left)});
        Check(probe->clicks == 1 && signProbe->clicks == 0 && false == canvas->FindComponentRaw<Component::Button2D>(sign)->clicked,
            "releasing over another button clicks neither");

        // 버튼 밖의 누름은 게임이 받는다.
        frameWith({MouseAt(190.0f, 90.0f), HeldButton(MouseButton::Left)});
        Check(poller->sawMouse && false == screen.IsPointerOverButton(), "a press away from every button reaches the game");
        frameWith({Released(MouseButton::Left)});

        // 월드 레이어의 버튼은 주 카메라로 맞춘다: 월드 (60, 0) 은 픽셀 (160, 50) 이다.
        frameWith({MouseAt(160.0f, 50.0f), HeldButton(MouseButton::Left)});
        frameWith({Released(MouseButton::Left)});
        Check(signProbe->clicks == 1 && probe->clicks == 1, "the world button is hit through the camera");

        // 앵커는 누름 사각형도 옮긴다: 오른쪽 위 앵커에서 (-20, -10) 은 픽셀 (180, 10) 이다.
        auto* playPlace = canvas->FindComponentRaw<Component::Transform2D>(play);
        playPlace->anchor = {1.0f, 1.0f};
        playPlace->position = {-20.0f, -10.0f};
        frameWith({MouseAt(10.0f, 10.0f)});
        frameWith({MouseAt(180.0f, 10.0f), HeldButton(MouseButton::Left)});
        frameWith({Released(MouseButton::Left)});
        Check(probe->clicks == 2, "an anchored button is pressed where it is drawn");

        // 꺼진 버튼은 누르지 못하지만 포인터는 가져간다.
        playButton->interactable = false;
        frameWith({HeldButton(MouseButton::Left)});
        Check(probe->downs == 4 && false == poller->sawMouse && playSprite->tint.A == playButton->disabledTint.A,
            "a disabled button is not pressed but still hides the pointer");
        frameWith({Released(MouseButton::Left)});
        Check(probe->clicks == 2, "and it does not click");
        playButton->interactable = true;

        // 손가락도 같다.
        frameWith({Touch(InputEventKind::TouchBegan, 180.0f, 10.0f)});
        Check(probe->downs == 5, "a finger presses the button");
        frameWith({Touch(InputEventKind::TouchEnded, 180.0f, 10.0f)});
        Check(probe->clicks == 3, "and lifting it there clicks");

        // 누름 사각형의 가장자리: 가운데 (180, 10), 반폭 20·반높이 10.
        frameWith({MouseAt(198.0f, 10.0f)});
        Check(playButton->hovered, "the edge of the hit box is still the button");
        frameWith({MouseAt(180.0f, 19.0f)});
        Check(playButton->hovered, "and so is its top edge");
        frameWith({MouseAt(158.0f, 10.0f)});
        Check(false == playButton->hovered, "just past the side is not");
        frameWith({MouseAt(180.0f, 22.0f)});
        Check(false == playButton->hovered, "nor just past the bottom");

        // 역투영 서비스: 화면 레이어는 기준 픽셀, 월드 레이어는 월드 좌표다. 거꾸로도 같은 자리다.
        const auto closeTo = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
        Vector2 point;
        Check(screen.ScreenToLayer({150.0f, 25.0f}, play->GetScriptHandle(), point) && closeTo(point.x, 50.0f) && closeTo(point.y, 25.0f),
            "a pixel lands on the screen layer in reference pixels");
        Check(screen.ScreenToLayer({160.0f, 50.0f}, sign->GetScriptHandle(), point) && closeTo(point.x, 60.0f) && closeTo(point.y, 0.0f),
            "and on a world layer in world units");
        Vector2 pixel;
        Check(screen.LayerToScreen({60.0f, 0.0f}, sign->GetScriptHandle(), pixel) && closeTo(pixel.x, 160.0f) && closeTo(pixel.y, 50.0f),
            "a world point goes back to its pixel");
        Check(false == screen.ScreenToLayer({1.0f, 1.0f}, Handle::GameObject{}, point), "an empty handle has no layer");

        framework.UnbindScriptContexts();
        framework.Shutdown();
        BindInputSystemContext({});
        BindInputServiceContext({});
    }
}

int RunInputChainTests()
{
    const bool echo = Log::GetEchoToConsole();
    Log::SetEchoToConsole(false);
    TestHandlersRunInLayerThenOrderThenExecutionOrder();
    TestAScriptJoinsTheChainAfterItStarts();
    TestBlockStopsEveryLowerHandlerAndThePolling();
    TestConsumingOneDeviceLeavesTheOthers();
    TestDisabledHandlersAreSkipped();
    TestChangingTheLayerOrderResortsTheChain();
    TestNamedScriptsJoinTheChainThroughTheirTypeInfo();
    TestAFrameWithoutTheChainIsNotBlocked();
    TestDispatchDoesNotAllocate();
    TestTheFrameworkDispatchesBeforeTheFixedSteps();
    TestButtonsTakeThePointerOnTheUiLayer();
    Log::SetEchoToConsole(echo);
    g_dispatchLog.Clear();
    std::cout << "Input chain tests passed.\n";
    return 0;
}
