#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Input/InputSystem.h>
#include <JBro/InputTypes/InputState.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **UI 위젯의 사용자 시나리오 - 잘못 잇고, 잘못 넣고, 도중에 지우는 사용자**(D-351·D-352, ui-plan §6.4). 정상 경로는 `InputChain` 묶음이 걷고,
// 여기는 엔진이 사용자의 실수에 터지거나 NaN 을 퍼뜨리거나 엉뚱한 오브젝트를 움직이지 않는지를 본다. 화면 200 x 100 = 기준, 1 유닛 = 1 픽셀.
namespace
{
    using namespace JBro;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool CloseTo(JBro::Float a, JBro::Float b)
    {
        return std::fabs(a - b) < 0.01f;
    }

    JBro::Bool Finite(Vector2 value)
    {
        return std::isfinite(value.x.Get()) && std::isfinite(value.y.Get());
    }

    const JBro::Float NaN = std::numeric_limits<float>::quiet_NaN();

    class PadPlatform final : public IPlatform
    {
    public:
        JBro::Bool Initialize(const JMemoryContext&) override { return true; }
        void Shutdown() override {}
        WindowHandle OpenPlatformWindow(const WindowDesc&) override { return {}; }
        void ClosePlatformWindow(WindowHandle) override {}
        SurfaceHandle CreateSurface(WindowHandle) override { return {}; }
        void PumpEvents() override {}
        JArrayView<InputEvent> GetInputEvents() const override { return {}; }
        void WaitForEvents(JBro::UInt32) override {}
        JBro::Bool ShouldClose(WindowHandle) const override { return false; }
        JBro::Bool GetWindowState(WindowHandle, WindowState&) const override { return false; }
        DynamicLibrary LoadDynamicLibrary(const char*) override { return {}; }
        void* GetSymbol(DynamicLibrary, const char*) override { return nullptr; }
        void UnloadDynamicLibrary(DynamicLibrary) override {}
        JBro::Bool PollGamepad(JBro::UInt32 slot, GamepadRawState& state) override
        {
            state = pads[slot];
            return state.connected;
        }
        void SetGamepadVibration(JBro::UInt32, JBro::Float, JBro::Float) override {}

        GamepadRawState pads[MaxGamepads] = {};
    };

    std::uint16_t PadBit(GamepadButton button)
    {
        return static_cast<std::uint16_t>(1u << static_cast<JBro::UInt32>(button));
    }

    InputEvent MouseAt(JBro::Float x, JBro::Float y)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseMove;
        event.x = x;
        event.y = y;
        return event;
    }

    InputEvent HeldButton(MouseButton button)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseButtonDown;
        event.button = button;
        return event;
    }

    InputEvent Released(MouseButton button)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseButtonUp;
        event.button = button;
        return event;
    }

    InputEvent Wheel(JBro::Float notches)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseWheel;
        event.y = notches;
        return event;
    }

    InputEvent Typed(JBro::UInt32 codePoint)
    {
        InputEvent event;
        event.kind = InputEventKind::Text;
        event.codePoint = codePoint;
        return event;
    }

    InputEvent Pressed(Key key)
    {
        InputEvent event;
        event.kind = InputEventKind::KeyDown;
        event.key = key;
        return event;
    }

    InputEvent ReleasedKey(Key key)
    {
        InputEvent event;
        event.kind = InputEventKind::KeyUp;
        event.key = key;
        return event;
    }

    // 게임이 보는 입력이다 - 위젯이 소비하면 비어 있다.
    class GamePoller final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "UiScenarioTests::GamePoller";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate() override
        {
            sawW = GetInputServices().Input.Keyboard().IsDown(Key::W);
            sawMouse = GetInputServices().Input.Mouse().IsDown(MouseButton::Left);
            sawSouth = GetInputServices().Input.Gamepad(0).IsDown(GamepadButton::South);
        }

        JBro::Bool sawW = false;
        JBro::Bool sawMouse = false;
        JBro::Bool sawSouth = false;
    };

    // 훅 안에서 제 오브젝트를 지우는 스크립트다 - 어느 훅에서 지울지는 필드로 고른다.
    JBro::Int32 g_hookCalls = 0;

    class SelfDestructor final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "UiScenarioTests::SelfDestructor";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnPointerDown() override
        {
            ++g_hookCalls;
            if (onDown)
            {
                GetGameObject().Destroy();
            }
        }

        void OnClick() override
        {
            ++g_hookCalls;
            if (onClick)
            {
                GetGameObject().Destroy();
            }
        }

        void OnValueChanged() override
        {
            ++g_hookCalls;
            if (onValue)
            {
                GetGameObject().Destroy();
            }
        }

        void OnSubmit() override
        {
            ++g_hookCalls;
            if (onSubmit)
            {
                GetGameObject().Destroy();
            }
        }

        JBro::Bool onDown = false;
        JBro::Bool onClick = false;
        JBro::Bool onValue = false;
        JBro::Bool onSubmit = false;
    };

    // 화면 레이어 하나와 입력·게임패드가 있는 프레임워크다. 시험마다 하나씩 만든다.
    // 입력 컨텍스트는 프레임워크를 만들기 **전에** 묶는다 - 프레임워크가 만들어질 때 붙잡는다(`InputChainTests` 와 같은 차례).
    struct BoundInput
    {
        explicit BoundInput(System::InputSystem& input)
        {
            BindInputSystemContext(input.GetSystemContext());
            BindInputServiceContext(input.GetServiceContext());
        }
    };

    struct Stage
    {
        System::InputSystem input;
        PadPlatform platform;
        BoundInput bound{input};
        Framework2D framework;
        FrameworkContext context;
        Canvas* canvas = nullptr;
        Layer* hud = nullptr;

        Stage()
        {
            platform.pads[0].connected = true;
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
            canvas = framework.GetCanvas();
            hud = &canvas->CreateLayer("HUD");
            hud->SetSpace(LayerSpace::Screen);
        }

        ~Stage()
        {
            framework.UnbindScriptContexts();
            framework.Shutdown();
            BindInputSystemContext({});
            BindInputServiceContext({});
        }

        Object::GameObject* Make(const char* name, Vector2 position, Object::GameObject* parent = nullptr)
        {
            Object::GameObject* object = canvas->CreateObject(name);
            canvas->SetObjectLayer(object, hud->GetId());
            if (parent != nullptr)
            {
                object->SetParent(parent);
            }
            canvas->AttachComponent<Component::Transform2D>(object)->position = position;
            return object;
        }

        Component::Transform2D* PlaceOf(Object::GameObject* object)
        {
            return canvas->FindComponentRaw<Component::Transform2D>(object);
        }

        void Frame(std::initializer_list<InputEvent> events)
        {
            Array<InputEvent> list;
            for (const InputEvent& event : events)
            {
                list.Add(event);
            }
            input.BeginFrame({list.Data(), static_cast<JBro::UInt32>(list.Size())});
            input.PollGamepads(platform, 1.0f / 60.0f);
            JBro::Testing::Tick(framework, 1.0f / 60.0f);
        }

        void Frames(JBro::Int32 count)
        {
            for (JBro::Int32 index = 0; index < count; ++index)
            {
                Frame({});
            }
        }
    };

    UInt32 CountCodePoints(ArrayView<const char> text)
    {
        UInt32 count = 0;
        for (std::size_t at = 0; at < text.Size(); ++at)
        {
            if ((static_cast<unsigned char>(text.Data()[at]) & 0xC0) != 0x80)
            {
                ++count;
            }
        }
        return count;
    }

    JBro::Bool TextIs(TextId id, const char* expected)
    {
        const ArrayView<const char> text = TextStore::Get().GetText(id);
        const std::size_t length = std::strlen(expected);
        return text.Size() == length && (length == 0 || std::memcmp(text.Data(), expected, length) == 0);
    }

    // **잘못 이은 참조는 아무것도 움직이지 않는다.** 슬라이더 손잡이·스크롤 내용·커서는 자식이어야 한다 - 자기 자신이나 남을 가리키면
    // 그 오브젝트가 매 프레임 끌려가지 않는다. 제대로 이으면 움직인다(같은 시험 안의 대조).
    void TestWrongWiringMovesNothing()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;

        // 슬라이더: 손잡이가 자기 자신 → 슬라이더가 제자리.
        Object::GameObject* volume = stage.Make("volume", {-40.0f, 20.0f});
        auto* slider = canvas->AttachComponent<Component::Slider2D>(volume);
        slider->size = {80.0f, 10.0f};
        slider->value = 0.5f;
        slider->handle = volume->GetScriptHandle();
        Object::GameObject* stray = stage.Make("stray", {50.0f, -30.0f});
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(volume)->position.x, -40.0f) && CloseTo(stage.PlaceOf(volume)->position.y, 20.0f),
            "a slider whose handle is itself stays where it is");
        // 손잡이가 남(자식이 아닌 오브젝트) → 그 오브젝트가 제자리.
        slider->handle = stray->GetScriptHandle();
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(stray)->position.x, 50.0f) && CloseTo(stage.PlaceOf(stray)->position.y, -30.0f),
            "a handle that is not a child is left alone");
        // 자식이면 값의 자리로 간다.
        Object::GameObject* knob = stage.Make("knob", {0.0f, 0.0f}, volume);
        slider->handle = knob->GetScriptHandle();
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(knob)->position.x, 0.0f) && CloseTo(stage.PlaceOf(knob)->position.y, 0.0f),
            "a child handle is placed at the value (0.5 of 80 wide is the middle)");

        // 스크롤 뷰: 내용이 자기 자신 → 휠을 굴려도 창이 제자리.
        Object::GameObject* window = stage.Make("window", {0.0f, -20.0f});
        auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
        mask->size = {100.0f, 50.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        scroll->contentSize = {100.0f, 400.0f};
        scroll->content = window->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(100.0f, 70.0f), Wheel(-3.0f)});
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(window)->position.x, 0.0f) && CloseTo(stage.PlaceOf(window)->position.y, -20.0f),
            "a scroll view whose content is itself does not drag itself around");
        // 내용이 남 → 남은 제자리.
        scroll->content = stray->GetScriptHandle();
        stage.Frame({Wheel(-3.0f)});
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(stray)->position.y, -30.0f), "content that is not a child is left alone");
        // 자식이면 움직인다.
        Object::GameObject* list = stage.Make("list", {0.0f, -175.0f}, window);
        scroll->content = list->GetScriptHandle();
        stage.Frame({Wheel(-3.0f)});
        Check(stage.PlaceOf(list)->position.y > -175.0f + 1.0f, "a child content moves on the wheel");
        // 마스크가 없는 스크롤 뷰는 스크롤하지 않고, 포인터도 게임으로 내려간다.
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));
        Object::GameObject* bare = stage.Make("bare", {-60.0f, -30.0f});
        auto* bareScroll = canvas->AttachComponent<Component::ScrollView2D>(bare);
        Object::GameObject* bareList = stage.Make("bareList", {0.0f, 0.0f}, bare);
        bareScroll->content = bareList->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(40.0f, 80.0f), Wheel(-3.0f), HeldButton(MouseButton::Left)});
        Check(CloseTo(stage.PlaceOf(bareList)->position.y, 0.0f) && poller->sawMouse, "a scroll view without a mask is not a widget at all");
        stage.Frame({Released(MouseButton::Left)});

        // 글자 칸: 커서가 표시 텍스트의 자식이 아니다 → 보이기는 바뀌지만 자리는 그대로.
        Object::GameObject* name = stage.Make("name", {40.0f, 20.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {60.0f, 20.0f};
        Object::GameObject* label = stage.Make("label", {0.0f, 0.0f}, name);
        canvas->AttachComponent<Component::Text2D>(label);
        field->display = label->GetScriptHandle().GetComponent<Handle::Text2D>();
        Object::GameObject* looseCaret = stage.Make("looseCaret", {70.0f, 40.0f});
        auto* caretSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(looseCaret);
        field->caret = looseCaret->GetScriptHandle().GetComponent<Handle::SpriteRenderer2D>();
        stage.Frames(2);
        stage.Frame({MouseAt(140.0f, 30.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frames(2);
        Check(field->focused && caretSprite->visible, "the field takes focus and shows the caret");
        Check(CloseTo(stage.PlaceOf(looseCaret)->position.x, 70.0f) && CloseTo(stage.PlaceOf(looseCaret)->position.y, 40.0f),
            "but a caret that is not the display's child is not moved");
        // 표시 텍스트가 자기 자신의 텍스트(글자 칸 오브젝트의 Text2D) - 허용한다. 글자가 옮겨진다.
        auto* ownText = canvas->AttachComponent<Component::Text2D>(name);
        field->display = name->GetScriptHandle().GetComponent<Handle::Text2D>();
        stage.Frame({Typed('h'), Typed('i')});
        stage.Frames(1);
        Check(TextIs(ownText->text, "hi"), "a display on the field's own object mirrors the text");
    }

    // **엉뚱한 값은 유한한 자리로 접힌다.** NaN·음수·0 크기·음수 상한·한 프레임의 글자 홍수에 엔진이 터지거나 NaN 을 퍼뜨리지 않는다.
    void TestHostileValuesStayFinite()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));

        // 슬라이더: NaN 값은 0 으로 보이고, 음수 크기는 절댓값이다.
        Object::GameObject* volume = stage.Make("volume", {-40.0f, 20.0f});
        auto* slider = canvas->AttachComponent<Component::Slider2D>(volume);
        slider->size = {-80.0f, -10.0f};
        slider->value = NaN;
        Object::GameObject* knob = stage.Make("knob", {0.0f, 0.0f}, volume);
        slider->handle = knob->GetScriptHandle();
        auto* fillSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(stage.Make("fill", {0.0f, 0.0f}, volume));
        fillSprite->drawMode = Component::SpriteDrawMode::Filled;
        slider->fill = fillSprite->GetOwner().GetComponent<Handle::SpriteRenderer2D>();
        stage.Frames(2);
        Check(Finite(stage.PlaceOf(knob)->position) && CloseTo(stage.PlaceOf(knob)->position.x, -40.0f), "a NaN value puts the handle at the start");
        Check(fillSprite->fillAmount == 0.0f, "and fills nothing");
        stage.Frame({MouseAt(80.0f, 30.0f), HeldButton(MouseButton::Left)});
        Check(slider->pressed && CloseTo(slider->value, 0.75f), "a negative size still takes the press as its absolute size");
        stage.Frame({Released(MouseButton::Left)});

        // 버튼: NaN 크기는 맞지 않는다 - 포인터가 게임으로 내려간다. 0 크기도 같다.
        Object::GameObject* ghost = stage.Make("ghost", {40.0f, 20.0f});
        auto* ghostButton = canvas->AttachComponent<Component::Button2D>(ghost);
        ghostButton->size = {NaN, NaN};
        stage.Frames(2);
        stage.Frame({MouseAt(140.0f, 30.0f), HeldButton(MouseButton::Left)});
        Check(false == ghostButton->hovered && poller->sawMouse, "a button with a NaN size is nowhere");
        stage.Frame({Released(MouseButton::Left)});
        ghostButton->size = {0.0f, 0.0f};
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(false == ghostButton->hovered && poller->sawMouse, "and so is a button with no size");
        stage.Frame({Released(MouseButton::Left)});

        // 글자 칸: 음수 상한은 상한이 없는 것이고, 한 프레임의 글자 홍수는 입력 상태의 칸 수에서 잘린다.
        Object::GameObject* name = stage.Make("name", {40.0f, -20.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {60.0f, 20.0f};
        field->maxLength = -5;
        stage.Frames(2);
        stage.Frame({MouseAt(140.0f, 70.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Array<InputEvent> flood;
        for (JBro::UInt32 index = 0; index < MaxTextPerFrame + 8; ++index)
        {
            flood.Add(Typed('a' + (index % 26)));
        }
        stage.input.BeginFrame({flood.Data(), static_cast<JBro::UInt32>(flood.Size())});
        stage.input.PollGamepads(stage.platform, 1.0f / 60.0f);
        JBro::Testing::Tick(stage.framework, 1.0f / 60.0f);
        Check(TextStore::Get().GetText(field->text).Size() == MaxTextPerFrame, "a frame keeps at most the input state's characters and a negative limit is no limit");
        // 서러게이트 반쪽과 제어 글자는 글자가 아니다.
        stage.Frame({Typed(0xD83D), Typed(0xDE00), Typed(0x09), Typed('!')});
        Check(TextStore::Get().GetText(field->text).Size() == MaxTextPerFrame + 1, "lone surrogates and control characters are dropped");
        field->maxLength = 2;
        stage.Frame({Pressed(Key::Backspace)});
        stage.Frame({ReleasedKey(Key::Backspace), Typed('z')});
        Check(TextStore::Get().GetText(field->text).Size() == MaxTextPerFrame, "backspace works past a limit that is already exceeded, typing does not");
        field->blinkRate = NaN;
        auto* caretSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(stage.Make("caret", {0.0f, 0.0f}, name));
        field->caret = caretSprite->GetOwner().GetComponent<Handle::SpriteRenderer2D>();
        stage.Frames(3);
        Check(caretSprite->visible && std::isfinite(field->blinkTime.Get()), "a NaN blink rate shows the caret steadily");
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});

        // 스크롤 뷰: NaN 감도·감속·내용 크기에 내용의 자리가 유한하다.
        Object::GameObject* window = stage.Make("window", {-50.0f, -20.0f});
        auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
        mask->size = {80.0f, 50.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        scroll->contentSize = {80.0f, 400.0f};
        scroll->scrollSensitivity = NaN;
        scroll->decelerationRate = NaN;
        Object::GameObject* list = stage.Make("list", {0.0f, -175.0f}, window);
        scroll->content = list->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(50.0f, 70.0f), Wheel(-3.0f)});
        Check(Finite(stage.PlaceOf(list)->position) && CloseTo(stage.PlaceOf(list)->position.y, -175.0f), "a NaN wheel sensitivity scrolls nothing");
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({MouseAt(50.0f, 50.0f)});
        stage.Frame({MouseAt(50.0f, 40.0f)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frames(10);
        Check(Finite(stage.PlaceOf(list)->position) && Finite(scroll->velocity) && scroll->velocity.y == 0.0f,
            "a NaN deceleration stops the content dead after the drag");
        scroll->contentSize = {NaN, NaN};
        scroll->scrollSensitivity = 40.0f;
        stage.Frame({Wheel(-3.0f)});
        stage.Frames(2);
        Check(Finite(stage.PlaceOf(list)->position), "a NaN content size leaves the position finite");

        // 레이아웃: 0 열·음수 칸·NaN 간격에 자식의 자리가 유한하다.
        Object::GameObject* grid = stage.Make("grid", {0.0f, 0.0f});
        auto* layout = canvas->AttachComponent<Component::Layout2D>(grid);
        layout->mode = Component::LayoutMode::Grid;
        layout->columns = 0;
        layout->cellSize = {-100.0f, 0.0f};
        Object::GameObject* cellA = stage.Make("a", {1.0f, 1.0f}, grid);
        Object::GameObject* cellB = stage.Make("b", {2.0f, 2.0f}, grid);
        stage.Frames(1);
        Check(Finite(stage.PlaceOf(cellA)->position) && Finite(stage.PlaceOf(cellB)->position)
                && CloseTo(stage.PlaceOf(cellB)->position.y, -4.0f),
            "zero columns means one column and a negative cell is its absolute size");
        layout->spacing = {NaN, 4.0f};
        stage.Frames(1);
        Check(Finite(stage.PlaceOf(cellA)->position) && Finite(stage.PlaceOf(cellB)->position), "a NaN spacing skips the layout instead of spreading NaN");
        layout->spacing = {4.0f, 4.0f};
        layout->pivot = {NaN, 1.0f};
        stage.Frames(1);
        Check(Finite(stage.PlaceOf(cellA)->position), "and so does a NaN pivot");

        // 마스크: NaN 크기는 자르지 않는다 - 안의 버튼은 그대로 눌린다.
        Object::GameObject* frame = stage.Make("frame", {40.0f, 20.0f});
        auto* frameMask = canvas->AttachComponent<Component::Mask2D>(frame);
        frameMask->size = {NaN, 10.0f};
        ghostButton->size = {40.0f, 20.0f};
        ghost->SetParent(frame);
        stage.PlaceOf(ghost)->position = {0.0f, 0.0f};
        stage.Frames(2);
        Check(false == frameMask->clipValid, "a NaN mask has no rectangle");
        stage.Frame({MouseAt(140.0f, 30.0f), HeldButton(MouseButton::Left)});
        Check(ghostButton->pressed && false == poller->sawMouse, "and does not clip the button inside");
        stage.Frame({Released(MouseButton::Left)});
    }

    // **도중에 지워도 선다.** 쥔 버튼·포커스 칸·끌던 스크롤 뷰가 사라지면 다음 프레임에 상태가 비고 입력이 게임으로 내려간다.
    // 훅 안에서 제 오브젝트를 지우는 스크립트는 그 프레임을 끝까지 돈다(파괴는 프레임 끝으로 미뤄진다).
    void TestDestroyingWidgetsMidInteraction()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));
        g_hookCalls = 0;

        // 누름 훅에서 제 오브젝트를 지우는 버튼.
        Object::GameObject* bomb = stage.Make("bomb", {-40.0f, 20.0f});
        canvas->AttachComponent<Component::Button2D>(bomb)->size = {40.0f, 20.0f};
        canvas->AttachComponent<Component::SpriteRenderer2D>(bomb);
        auto* bombScript = canvas->AttachScript<SelfDestructor>(bomb);
        bombScript->onDown = true;
        const Handle::GameObject bombHandle = bomb->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(60.0f, 30.0f), HeldButton(MouseButton::Left)});
        Check(g_hookCalls == 1 && false == bombHandle.IsValid(), "the down hook ran once and the object is gone after the frame");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(poller->sawMouse && g_hookCalls == 1, "the next press reaches the game and no hook runs on the dead object");
        stage.Frame({Released(MouseButton::Left)});

        // 클릭 훅에서 지우는 버튼 - 뗄 때 지워지고 그 뒤 호버·포커스가 남지 않는다.
        Object::GameObject* mine = stage.Make("mine", {-40.0f, 20.0f});
        auto* mineButton = canvas->AttachComponent<Component::Button2D>(mine);
        mineButton->size = {40.0f, 20.0f};
        auto* mineScript = canvas->AttachScript<SelfDestructor>(mine);
        mineScript->onClick = true;
        const Handle::GameObject mineHandle = mine->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(g_hookCalls == 3 && false == mineHandle.IsValid(), "the down and click hooks ran and the object is gone");
        stage.Frame({});
        Check(poller->sawMouse == false, "nothing is pressed any more");
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(poller->sawMouse, "and the pointer reaches the game");
        stage.Frame({Released(MouseButton::Left)});

        // 쥐고 있는 동안 시험이(프레임 밖에서) 지운다 - 바로 지워진다.
        Object::GameObject* held = stage.Make("held", {-40.0f, 20.0f});
        auto* heldButton = canvas->AttachComponent<Component::Button2D>(held);
        heldButton->size = {40.0f, 20.0f};
        stage.Frames(2);
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(heldButton->pressed, "the button is held");
        Check(canvas->DestroyObject(held), "the test destroys it between frames");
        stage.Frame({});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(poller->sawMouse, "after the held button is gone the pointer reaches the game");
        stage.Frame({Released(MouseButton::Left)});

        // 포커스 칸을 지운다 - 키보드가 게임으로 내려간다.
        Object::GameObject* name = stage.Make("name", {40.0f, 20.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {60.0f, 20.0f};
        stage.Frames(2);
        stage.Frame({MouseAt(140.0f, 30.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->focused, "the field has focus");
        stage.Frame({Pressed(Key::W)});
        Check(false == poller->sawW, "and the keyboard is consumed");
        Check(canvas->DestroyObject(name), "the field is destroyed between frames");
        stage.Frame({});
        Check(poller->sawW, "once it is gone the keyboard reaches the game");
        stage.Frame({ReleasedKey(Key::W)});

        // 제출 훅에서 제 오브젝트를 지우는 칸.
        Object::GameObject* form = stage.Make("form", {40.0f, 20.0f});
        auto* formField = canvas->AttachComponent<Component::InputField2D>(form);
        formField->size = {60.0f, 20.0f};
        auto* formScript = canvas->AttachScript<SelfDestructor>(form);
        formScript->onSubmit = true;
        const Handle::GameObject formHandle = form->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(140.0f, 30.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({Typed('x'), Pressed(Key::Enter)});
        Check(false == formHandle.IsValid(), "submitting destroyed the form");
        stage.Frame({ReleasedKey(Key::Enter), Pressed(Key::W)});
        Check(poller->sawW, "and the keyboard is free again");
        stage.Frame({ReleasedKey(Key::W)});

        // 값 훅에서 제 오브젝트를 지우는 슬라이더 - 끄는 중에 사라진다.
        Object::GameObject* volume = stage.Make("volume", {-40.0f, -20.0f});
        auto* slider = canvas->AttachComponent<Component::Slider2D>(volume);
        slider->size = {80.0f, 10.0f};
        auto* volumeScript = canvas->AttachScript<SelfDestructor>(volume);
        volumeScript->onValue = true;
        const Handle::GameObject volumeHandle = volume->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(60.0f, 70.0f), HeldButton(MouseButton::Left)});
        Check(false == volumeHandle.IsValid(), "the press changed the value and the slider destroyed itself");
        stage.Frame({MouseAt(80.0f, 70.0f)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(poller->sawMouse, "the pointer reaches the game after the slider is gone");
        stage.Frame({Released(MouseButton::Left)});

        // 끌던 스크롤 뷰를 지운다.
        Object::GameObject* window = stage.Make("window", {0.0f, -20.0f});
        canvas->AttachComponent<Component::Mask2D>(window)->size = {100.0f, 50.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        scroll->contentSize = {100.0f, 400.0f};
        Object::GameObject* list = stage.Make("list", {0.0f, -175.0f}, window);
        scroll->content = list->GetScriptHandle();
        stage.Frames(2);
        stage.Frame({MouseAt(100.0f, 70.0f), HeldButton(MouseButton::Left)});
        stage.Frame({MouseAt(100.0f, 50.0f)});
        stage.Frame({MouseAt(100.0f, 40.0f)});
        Check(scroll->dragging, "the scroll view is being dragged");
        Check(canvas->DestroyObject(window), "the window is destroyed mid drag");
        stage.Frame({MouseAt(100.0f, 30.0f)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(poller->sawMouse, "the pointer reaches the game after the scroll view is gone");
        stage.Frame({Released(MouseButton::Left)});

        // 게임패드 포커스의 버튼을 지운다 - 다음 탐색은 왼쪽 위부터다.
        Object::GameObject* first = stage.Make("first", {-40.0f, 20.0f});
        auto* firstButton = canvas->AttachComponent<Component::Button2D>(first);
        firstButton->size = {40.0f, 20.0f};
        Object::GameObject* second = stage.Make("second", {40.0f, 20.0f});
        auto* secondButton = canvas->AttachComponent<Component::Button2D>(second);
        secondButton->size = {40.0f, 20.0f};
        stage.Frames(2);
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(firstButton->focused, "the first navigation focuses the top-left button");
        Check(canvas->DestroyObject(first), "which is then destroyed");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        Check(false == secondButton->pressed && poller->sawSouth, "south with no focus presses nothing and reaches the game");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        Check(secondButton->focused, "the next navigation starts over from the top-left");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
    }

    // **같은 프레임의 두 손짓**: 십자키와 South 를 같은 프레임에 누르면 옮긴 포커스가 눌리고, 마우스로 포커스를 비운 프레임의 South 는 아무것도
    // 누르지 않는다. 스크립트가 글자 칸의 `focused` 를 거짓으로 쓰면 포커스를 놓고, 둘이 참이면 첫 것만 받는다.
    void TestFocusChangesInsideOneFrame()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));
        Object::GameObject* left = stage.Make("left", {-40.0f, 20.0f});
        auto* leftButton = canvas->AttachComponent<Component::Button2D>(left);
        leftButton->size = {40.0f, 20.0f};
        auto* leftScript = canvas->AttachScript<SelfDestructor>(left);
        Object::GameObject* right = stage.Make("right", {40.0f, 20.0f});
        auto* rightButton = canvas->AttachComponent<Component::Button2D>(right);
        rightButton->size = {40.0f, 20.0f};
        auto* rightScript = canvas->AttachScript<SelfDestructor>(right);
        g_hookCalls = 0;
        stage.Frames(2);
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(leftButton->focused, "the left button has focus");
        // 오른쪽 + South 를 같은 프레임에.
        stage.platform.pads[0].buttons = static_cast<std::uint16_t>(PadBit(GamepadButton::DPadRight) | PadBit(GamepadButton::South));
        stage.Frame({});
        Check(rightButton->focused && rightButton->pressed && false == leftButton->pressed, "the focus moves first and the press goes to the new focus");
        Check(false == poller->sawSouth, "the held confirm button belongs to the UI");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(rightButton->clicked && g_hookCalls == 2, "letting go clicks the right button (down and click hooks)");
        (void)leftScript;
        (void)rightScript;
        // 빈 곳을 누르는 프레임의 South.
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({MouseAt(100.0f, 90.0f), HeldButton(MouseButton::Left)});
        Check(false == rightButton->pressed && false == leftButton->pressed && g_hookCalls == 2, "south on the frame the pointer clears the focus presses nothing");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({Released(MouseButton::Left)});

        // 글자 칸 둘의 `focused` 를 스크립트가 동시에 참으로.
        Object::GameObject* nameA = stage.Make("nameA", {-40.0f, -20.0f});
        auto* fieldA = canvas->AttachComponent<Component::InputField2D>(nameA);
        fieldA->size = {60.0f, 20.0f};
        Object::GameObject* nameB = stage.Make("nameB", {40.0f, -20.0f});
        auto* fieldB = canvas->AttachComponent<Component::InputField2D>(nameB);
        fieldB->size = {60.0f, 20.0f};
        stage.Frames(2);
        fieldA->focused = true;
        fieldB->focused = true;
        stage.Frame({});
        Check(fieldA->focused != fieldB->focused, "only one field can hold the focus");
        const JBro::Bool aWon = fieldA->focused;
        stage.Frame({});
        Check(fieldA->focused == aWon && fieldB->focused != aWon, "and it does not bounce between them");
        // 스크립트가 거짓으로 쓰면 놓는다.
        (aWon ? fieldA : fieldB)->focused = false;
        stage.Frame({Pressed(Key::W)});
        Check(false == fieldA->focused && false == fieldB->focused && poller->sawW, "writing false drops the focus and frees the keyboard");
        stage.Frame({ReleasedKey(Key::W)});
    }

    // **자동 반복과 가운데 편집**(D-354): ←·→·Home·End 가 커서를 옮기고 글자는 커서 자리에 끼며 Delete 는 뒤 글자를 지운다. 쥔 Backspace 와
    // 십자키는 지연(0.4 초) 동안 한 번이고 그 뒤 간격(0.05 초·0.1 초)마다 되풀이한다. 놓으면 멈춘다.
    void TestHeldKeysRepeatAndTheCaretMoves()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        Object::GameObject* name = stage.Make("name", {0.0f, -30.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {120.0f, 30.0f};
        stage.Frames(2);
        stage.Frame({MouseAt(100.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->focused && field->caretIndex == 0, "the empty field takes focus with the caret at the front");
        stage.Frame({Typed('a'), Typed('b'), Typed('c')});
        Check(TextIs(field->text, "abc") && field->caretIndex == 3, "typing leaves the caret at the end");
        stage.Frame({Pressed(Key::Left)});
        stage.Frame({ReleasedKey(Key::Left), Pressed(Key::Left)});
        stage.Frame({ReleasedKey(Key::Left)});
        Check(field->caretIndex == 1, "left moves the caret back one code point per press");
        stage.Frame({Typed('X')});
        Check(TextIs(field->text, "aXbc") && field->caretIndex == 2, "a typed character goes in at the caret");
        stage.Frame({Pressed(Key::Delete)});
        stage.Frame({ReleasedKey(Key::Delete)});
        Check(TextIs(field->text, "aXc") && field->caretIndex == 2, "delete removes the code point after the caret");
        stage.Frame({Pressed(Key::Home)});
        stage.Frame({ReleasedKey(Key::Home), Pressed(Key::Backspace)});
        stage.Frame({ReleasedKey(Key::Backspace)});
        Check(TextIs(field->text, "aXc") && field->caretIndex == 0, "backspace at the front removes nothing");
        stage.Frame({Pressed(Key::Right)});
        stage.Frame({ReleasedKey(Key::Right), Typed(0xD55C)});
        Check(TextIs(field->text, "a\xED\x95\x9CXc") && field->caretIndex == 2, "a multi-byte character goes in after the moved caret as one code point");
        stage.Frame({Pressed(Key::End)});
        stage.Frame({ReleasedKey(Key::End)});
        Check(field->caretIndex == 4, "end moves the caret to the end");
        field->caretIndex = 99;
        stage.Frame({Typed('!')});
        Check(TextIs(field->text, "a\xED\x95\x9CXc!") && field->caretIndex == 5, "a caret written past the end is clamped to the end");

        // 쥔 Backspace: 누른 프레임에 한 글자, 0.4 초 동안은 그대로, 그 뒤 0.05 초(3 프레임)마다 한 글자.
        stage.Frame({Pressed(Key::Backspace)});
        Check(TextIs(field->text, "a\xED\x95\x9CXc"), "the press removes one code point");
        stage.Frames(20);
        Check(TextIs(field->text, "a\xED\x95\x9CXc"), "holding it does nothing before the delay");
        stage.Frames(10);
        const ArrayView<const char> afterHold = TextStore::Get().GetText(field->text);
        const UInt32 leftOver = CountCodePoints(afterHold);
        Check(leftOver == 1 || leftOver == 2, "after half a second the held key has repeated two or three times");
        stage.Frame({ReleasedKey(Key::Backspace)});
        stage.Frames(30);
        Check(CountCodePoints(TextStore::Get().GetText(field->text)) == leftOver, "letting go stops the repeat");

        // 쥔 십자키: 누른 프레임에 한 칸, 0.4 초 뒤부터 0.1 초(6 프레임)마다 한 칸.
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        Check(false == field->focused, "escape clears the focus");
        Object::GameObject* first = stage.Make("first", {-60.0f, 30.0f});
        auto* firstButton = canvas->AttachComponent<Component::Button2D>(first);
        firstButton->size = {30.0f, 20.0f};
        Object::GameObject* second = stage.Make("second", {0.0f, 30.0f});
        auto* secondButton = canvas->AttachComponent<Component::Button2D>(second);
        secondButton->size = {30.0f, 20.0f};
        Object::GameObject* third = stage.Make("third", {60.0f, 30.0f});
        auto* thirdButton = canvas->AttachComponent<Component::Button2D>(third);
        thirdButton->size = {30.0f, 20.0f};
        stage.Frames(2);
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        Check(firstButton->focused, "the press focuses the top-left button");
        stage.Frames(20);
        Check(firstButton->focused, "holding right does nothing before the delay");
        stage.Frames(8);
        Check(secondButton->focused, "then it moves one widget per interval");
        stage.Frames(6);
        Check(thirdButton->focused, "and again after the next interval");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadLeft);
        stage.Frame({});
        Check(secondButton->focused, "a fresh press moves at once");
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        // 스틱도 같다 - 문턱을 넘긴 채 두면 지연 뒤 되풀이한다.
        stage.platform.pads[0].axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = -1.0f;
        stage.Frame({});
        Check(firstButton->focused, "the stick moves once when it crosses the threshold");
        stage.platform.pads[0].axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 1.0f;
        stage.Frame({});
        Check(secondButton->focused, "and once more when it crosses the other way");
        stage.Frames(28);
        Check(thirdButton->focused, "holding it past the delay repeats");
        stage.platform.pads[0].axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 0.0f;
        stage.Frame({});
    }
}

JBro::Int32 RunUiScenarioTests()
{
    const JBro::Bool echo = Log::GetEchoToConsole();
    Log::SetEchoToConsole(false);
    TestWrongWiringMovesNothing();
    TestHostileValuesStayFinite();
    TestDestroyingWidgetsMidInteraction();
    TestFocusChangesInsideOneFrame();
    TestHeldKeysRepeatAndTheCaretMoves();
    Log::SetEchoToConsole(echo);
    std::cout << "UI scenario tests passed.\n";
    return 0;
}
