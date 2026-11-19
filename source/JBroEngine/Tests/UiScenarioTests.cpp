#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/CanvasGroup2D.h>
#include <JBro/Framework2D/Component/Draggable2D.h>
#include <JBro/Framework2D/Component/DropTarget2D.h>
#include <JBro/Framework2D/Component/Dropdown2D.h>
#include <JBro/Framework2D/Component/Modal2D.h>
#include <JBro/Framework2D/Component/Navigation2D.h>
#include <JBro/Framework2D/Component/SafeArea2D.h>
#include <JBro/Framework2D/Component/TabGroup2D.h>
#include <JBro/Framework2D/Component/Tooltip2D.h>
#include <JBro/Framework2D/Component/VirtualKeyboard2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/ToggleGroup2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Handle/Button2D.h>
#include <JBro/Framework2D/Handle/InputField2D.h>
#include <JBro/Framework2D/Handle/Layout2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/ScreenUi/Rendering/GroupState2D.h>
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

    InputEvent PressedWith(Key key, KeyModifiers modifiers)
    {
        InputEvent event = Pressed(key);
        event.modifiers = modifiers;
        return event;
    }

    InputEvent Composition(InputEventKind kind, JBro::UInt32 codePoint = 0)
    {
        InputEvent event;
        event.kind = kind;
        event.codePoint = codePoint;
        return event;
    }

    // 글자 입력 호스트의 가짜다(D-355) - 조합 창 자리를 받아 두고 클립보드 글자를 든다.
    class FakeTextInput final : public ITextInputHost
    {
    public:
        void SetTextInputArea(JBro::Float pixelX, JBro::Float pixelY, JBro::Float lineHeight) override
        {
            ++areaCalls;
            lastX = pixelX;
            lastY = pixelY;
            lastHeight = lineHeight;
        }

        JBro::Bool ReadClipboardText(String& utf8) override
        {
            if (clipboard.empty())
            {
                return false;
            }
            utf8 = clipboard;
            return true;
        }

        JBro::Bool WriteClipboardText(const char* utf8, std::size_t size) override
        {
            clipboard.assign(utf8, size);
            ++writes;
            return true;
        }

        JBro::Int32 areaCalls = 0;
        JBro::Int32 writes = 0;
        JBro::Float lastX = 0.0f;
        JBro::Float lastY = 0.0f;
        JBro::Float lastHeight = 0.0f;
        String clipboard;
    };

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

    // 끌어 놓기의 훅을 센다(D-357). `destroyOnEnd` 면 끌기가 끝나는 훅에서 제 오브젝트를 지운다.
    class DragProbe final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "UiScenarioTests::DragProbe";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnDragBegin() override
        {
            ++begins;
        }

        void OnDragEnd() override
        {
            ++ends;
            if (destroyOnEnd)
            {
                GetGameObject().Destroy();
            }
        }

        void OnDrop() override
        {
            ++drops;
        }

        JBro::Int32 begins = 0;
        JBro::Int32 ends = 0;
        JBro::Int32 drops = 0;
        JBro::Bool destroyOnEnd = false;
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
        FakeTextInput textInput;
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
            context.textInput = &textInput;
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

    // **선택·클립보드·조합**(D-355). 폰트가 없어 레이아웃이 없는 칸이다 - 포인터는 글자 끝에 커서를 두고, 선택은 키보드로 한다.
    void TestSelectionClipboardAndComposition()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        Object::GameObject* name = stage.Make("name", {0.0f, -30.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {120.0f, 30.0f};
        Object::GameObject* label = stage.Make("label", {-50.0f, 0.0f}, name);
        auto* text = canvas->AttachComponent<Component::Text2D>(label);
        field->display = label->GetScriptHandle().GetComponent<Handle::Text2D>();
        Object::GameObject* highlight = stage.Make("highlight", {}, label);
        auto* highlightSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(highlight);
        field->selection = highlight->GetScriptHandle().GetComponent<Handle::SpriteRenderer2D>();
        Object::GameObject* underline = stage.Make("underline", {}, label);
        auto* underlineSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(underline);
        field->composition = underline->GetScriptHandle().GetComponent<Handle::SpriteRenderer2D>();
        stage.Frames(2);
        stage.Frame({MouseAt(100.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->focused && field->caretIndex == 0 && field->selectionAnchor == -1, "the field takes focus without a selection");
        Check(stage.textInput.areaCalls > 0 && CloseTo(stage.textInput.lastX, 50.0f) && CloseTo(stage.textInput.lastY, 80.0f),
            "a focused field tells the host where its text is, in screen pixels");
        stage.Frame({Typed('a'), Typed('b'), Typed('c'), Typed('d'), Typed('e'), Typed('f')});
        Check(TextIs(field->text, "abcdef") && field->caretIndex == 6, "typing fills the field");
        // 되돌리기(D-361): Ctrl+Z 가 마지막 고치기 전으로, 다시 누르면 다시 한다. 고친 적이 없으면 아무것도 아니다.
        Check(field->undoValid && TextIs(field->undoText, "") && field->undoCaret == 0, "the edit kept the text before it");
        stage.Frame({PressedWith(Key::Z, KeyModifierControl)});
        Check(TextIs(field->text, "") && field->caretIndex == 0 && field->changed, "ctrl+z undoes the last edit");
        stage.Frame({ReleasedKey(Key::Z), PressedWith(Key::Z, KeyModifierControl)});
        Check(TextIs(field->text, "abcdef") && field->caretIndex == 6, "ctrl+z again redoes it with the caret where it was");
        stage.Frame({ReleasedKey(Key::Z), Pressed(Key::End)});
        stage.Frame({ReleasedKey(Key::End)});
        Check(field->caretIndex == 6, "end goes back to the end");

        // Shift+← 둘: 선택 [4, 6). 글자가 오면 선택을 대신한다.
        stage.Frame({PressedWith(Key::Left, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::Left), PressedWith(Key::Left, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::Left)});
        Check(field->caretIndex == 4 && field->selectionAnchor == 6, "shift+left extends the selection from the caret");
        Check(highlightSprite->visible == false, "without a layout the selection sprite stays hidden");
        stage.Frame({Typed('X')});
        Check(TextIs(field->text, "abcdX") && field->caretIndex == 5 && field->selectionAnchor == -1, "a typed character replaces the selection");

        // Ctrl+A 전부, Ctrl+C 복사, → 로 접기, Ctrl+V 붙여넣기.
        stage.Frame({PressedWith(Key::A, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::A)});
        Check(field->selectionAnchor == 0 && field->caretIndex == 5, "ctrl+a selects everything");
        stage.Frame({PressedWith(Key::C, KeyModifierControl), Typed(0x03)});
        stage.Frame({ReleasedKey(Key::C)});
        Check(stage.textInput.clipboard == "abcdX" && TextIs(field->text, "abcdX"), "ctrl+c copies the selection and the control character is not typed");
        stage.Frame({Pressed(Key::Right)});
        stage.Frame({ReleasedKey(Key::Right)});
        Check(field->selectionAnchor == -1 && field->caretIndex == 5, "right without shift collapses the selection to its end");
        stage.Frame({PressedWith(Key::V, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::V)});
        Check(TextIs(field->text, "abcdXabcdX") && field->caretIndex == 10, "ctrl+v pastes at the caret");
        stage.Frame({PressedWith(Key::A, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::A), PressedWith(Key::X, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::X)});
        Check(TextIs(field->text, "") && stage.textInput.clipboard == "abcdXabcdX" && field->caretIndex == 0, "ctrl+x cuts the selection to the clipboard");

        // 상한과 제어 글자: 붙여넣기는 상한에서 멈추고 개행·탭을 버린다. 깨진 UTF-8 은 건너뛴다.
        field->maxLength = 3;
        stage.textInput.clipboard = "a\nb\tcd";
        stage.Frame({PressedWith(Key::V, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::V)});
        Check(TextIs(field->text, "abc") && field->caretIndex == 3, "pasting drops control characters and stops at maxLength");
        field->maxLength = 0;
        stage.textInput.clipboard = "\xFF\xC3\xA9";
        stage.Frame({PressedWith(Key::V, KeyModifierControl)});
        stage.Frame({ReleasedKey(Key::V)});
        Check(TextIs(field->text, "abc\xC3\xA9"), "a broken byte is skipped and the valid glyph after it is pasted");

        // Shift+Home 으로 앞까지 고르고 Backspace 로 지운다. Shift+End 는 뒤까지.
        stage.Frame({PressedWith(Key::Left, KeyModifierNone)});
        stage.Frame({ReleasedKey(Key::Left), PressedWith(Key::Home, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::Home)});
        Check(field->caretIndex == 0 && field->selectionAnchor == 3, "shift+home selects back to the front");
        stage.Frame({Pressed(Key::Backspace)});
        stage.Frame({ReleasedKey(Key::Backspace)});
        Check(TextIs(field->text, "\xC3\xA9") && field->caretIndex == 0 && field->selectionAnchor == -1, "backspace removes the selection");
        stage.Frame({PressedWith(Key::End, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::End), Pressed(Key::Delete)});
        stage.Frame({ReleasedKey(Key::Delete)});
        Check(TextIs(field->text, "") && field->selectionAnchor == -1, "delete removes the selection too");

        // 스크립트가 쓴 선택은 글자 수 안으로 가둔다. 같은 자리면 선택이 없다.
        stage.Frame({Typed('x'), Typed('y')});
        field->selectionAnchor = 99;
        field->caretIndex = 0;
        stage.Frame({});
        Check(field->selectionAnchor == 2 && field->caretIndex == 0, "a selection anchor past the end is clamped");
        field->selectionAnchor = 0;
        stage.Frame({});
        Check(field->selectionAnchor == -1, "an anchor at the caret is no selection");

        // 조합: 조합 글자는 보이는 텍스트에만 끼고 저장 글자는 그대로다. 끝나면 결과 글자가 글자로 온다.
        stage.Frame({Pressed(Key::End)});
        stage.Frame({ReleasedKey(Key::End), Composition(InputEventKind::CompositionBegin), Composition(InputEventKind::Composition, 0x3131)});
        Check(field->composing && TextIs(field->text, "xy") && TextIs(text->text, "xy\xE3\x84\xB1"), "the composition shows after the caret but is not stored");
        stage.Frame({});
        Check(field->composing && TextIs(text->text, "xy\xE3\x84\xB1"), "it stays while the composition lasts");
        stage.Frame({Composition(InputEventKind::CompositionBegin), Composition(InputEventKind::Composition, 0xAC00)});
        Check(TextIs(text->text, "xy\xEA\xB0\x80"), "a changed composition replaces the shown one");
        stage.Frame({Composition(InputEventKind::CompositionEnd), Typed(0xAC00)});
        Check(false == field->composing && TextIs(field->text, "xy\xEA\xB0\x80") && TextIs(text->text, "xy\xEA\xB0\x80") && field->caretIndex == 3,
            "the finished character is stored and the display matches");
        Check(false == underlineSprite->visible, "without a layout the composition underline stays hidden");
        stage.Frame({Pressed(Key::Home)});
        stage.Frame({ReleasedKey(Key::Home), Composition(InputEventKind::CompositionBegin), Composition(InputEventKind::Composition, 0x3134)});
        Check(TextIs(text->text, "\xE3\x84\xB4xy\xEA\xB0\x80") && TextIs(field->text, "xy\xEA\xB0\x80"), "the composition sits at the caret, not at the end");
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        Check(false == field->focused && false == field->composing && TextIs(text->text, "xy\xEA\xB0\x80"), "losing focus drops the shown composition");
        const JBro::Int32 callsBefore = stage.textInput.areaCalls;
        stage.Frames(2);
        Check(stage.textInput.areaCalls == callsBefore, "an unfocused field does not tell the host anything");

        // 포인터: 레이아웃이 없으면 누른 자리의 커서는 글자 끝이고, 끌어도 선택이 없다.
        stage.Frame({MouseAt(60.0f, 80.0f), HeldButton(MouseButton::Left)});
        Check(field->focused && field->caretIndex == 3 && field->selectionAnchor == -1, "a press without a layout puts the caret at the end");
        stage.Frame({MouseAt(120.0f, 80.0f)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->caretIndex == 3 && field->selectionAnchor == -1, "dragging without a layout selects nothing");
        stage.Frame({MouseAt(NaN, NaN), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->caretIndex == 3 && Finite(stage.PlaceOf(highlight)->position), "a NaN press leaves the caret and the highlight finite");
        // 더블 클릭(D-361): 레이아웃이 없어 커서는 끝이다 - 끝의 단어(공백 뒤)를 고른다. 0.3 초가 지난 두 번째 누름은 더블 클릭이 아니다.
        TextStore::Get().Assign(field->text, "ab cd", 5);
        field->caretIndex = 5;
        stage.Frame({MouseAt(100.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->caretIndex == 5 && field->selectionAnchor == -1, "the first press only places the caret");
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(field->caretIndex == 5 && field->selectionAnchor == 3, "the second press right after selects the word at the caret");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frames(20);
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(field->selectionAnchor == -1, "a press a third of a second later is a single press again");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(104.0f, 83.0f), HeldButton(MouseButton::Left)});
        Check(field->selectionAnchor == 3, "a second press within four pixels still counts");
        stage.Frame({Released(MouseButton::Left)});
        TextStore::Get().Assign(field->text, "ab ", 3);
        field->caretIndex = 3;
        stage.Frames(20);
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(field->selectionAnchor == -1, "a double click on a space selects nothing");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({Typed('q')});
        stage.Frame({PressedWith(Key::Z, KeyModifierControl)});
        Check(TextIs(field->text, "ab ") && field->caretIndex == 3, "undo after a typed character restores the text");
        stage.Frame({ReleasedKey(Key::Z)});
        // 선택 스프라이트가 없으면 보이는 텍스트의 강조 범위가 선택을 따른다(D-362) - 줄마다 칠하는 쪽이다.
        Check(text->highlightEnd <= text->highlightBegin, "with a selection sprite the display's highlight is left alone");
        field->selection = {};
        // 앞에서 Esc 로 끊긴 조합이 입력 상태에 남아 있다 - 끝내야 선택이 보인다(조합 중에는 선택이 없다).
        stage.Frame({Composition(InputEventKind::CompositionEnd)});
        stage.Frame({PressedWith(Key::Left, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::Left), PressedWith(Key::Left, KeyModifierShift)});
        stage.Frame({ReleasedKey(Key::Left)});
        Check(field->selectionAnchor == 3 && field->caretIndex == 1 && text->highlightBegin == 1 && text->highlightEnd == 3,
            "without one the display highlights the selected code points");
        stage.Frame({Pressed(Key::End)});
        stage.Frame({ReleasedKey(Key::End)});
        Check(text->highlightEnd <= text->highlightBegin, "collapsing the selection clears the highlight");
    }

    // **스크립트가 만드는 위젯과 커서 API**(D-355). 월드 레이어에 붙인 위젯은 픽셀 기본값을 유닛으로 나눈다. 핸들로 커서·선택을 쓴다.
    void TestScriptMadeWidgetsAndCaretApi()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        Layer* world = &canvas->CreateLayer("World");
        Object::GameObject* ground = canvas->CreateObject("ground");
        canvas->SetObjectLayer(ground, world->GetId());
        canvas->AttachComponent<Component::Transform2D>(ground);
        Object::GameObject* panel = stage.Make("panel", {});
        stage.Frames(1);
        Handle::Button2D worldButton = ground->GetScriptHandle().AddComponent<Handle::Button2D>();
        Handle::Button2D screenButton = panel->GetScriptHandle().AddComponent<Handle::Button2D>();
        Check(worldButton.IsValid() && CloseTo(worldButton.GetSize().x, 1.6f) && CloseTo(worldButton.GetSize().y, 0.48f),
            "a script-made button on a world layer is 1.6 x 0.48 units");
        Check(screenButton.IsValid() && CloseTo(screenButton.GetSize().x, 160.0f) && CloseTo(screenButton.GetSize().y, 48.0f),
            "and 160 x 48 pixels on a screen layer");
        Handle::Layout2D worldLayout = ground->GetScriptHandle().AddComponent<Handle::Layout2D>();
        Check(worldLayout.IsValid() && CloseTo(worldLayout.GetCellSize().x, 1.0f) && CloseTo(worldLayout.GetSpacing().x, 0.04f),
            "the layout's cell size and spacing are divided too");
        Check(CloseTo(worldLayout.GetPivot().y, 1.0f), "non-pixel fields are untouched");

        Object::GameObject* name = stage.Make("name", {0.0f, -30.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {120.0f, 30.0f};
        Handle::InputField2D handle = name->GetScriptHandle().GetComponent<Handle::InputField2D>();
        stage.Frames(2);
        handle.SetFocused(true);
        stage.Frame({});
        stage.Frame({Typed('a'), Typed('b'), Typed('c'), Typed('d')});
        Check(handle.GetCaretIndex() == 4 && handle.GetSelectionAnchor() == -1 && false == handle.IsComposing(), "the handle reads the caret");
        handle.SetSelection(1, 3);
        stage.Frame({});
        Check(field->selectionAnchor == 1 && field->caretIndex == 3, "the handle writes a selection");
        stage.Frame({Typed('Z')});
        Check(TextIs(field->text, "aZd") && handle.GetCaretIndex() == 2, "typing replaces the scripted selection");
        handle.SetCaretIndex(-5);
        stage.Frame({});
        Check(handle.GetCaretIndex() == 0, "a negative caret is the front");
        handle.SetSelection(-3, 99);
        stage.Frame({});
        Check(handle.GetSelectionAnchor() == -1 && handle.GetCaretIndex() == 3, "a negative anchor is no selection and the caret is clamped");
        stage.Frame({Composition(InputEventKind::CompositionBegin), Composition(InputEventKind::Composition, 0x3131)});
        Check(handle.IsComposing(), "the handle sees the composition");
        stage.Frame({Composition(InputEventKind::CompositionEnd)});
        Check(false == handle.IsComposing(), "and its end");
    }

    // **토글 그룹·드롭다운·전이**(D-355, B 1 순위). 화면 200 x 100 = 기준. 그룹의 토글 셋은 y 30(픽셀 y 20)에 20 x 16, 드롭다운은 (0, -40)(픽셀 (100, 90))에 100 x 16 이고
    // 목록은 그 자식 (0, 45) 에, 항목 둘은 목록의 자식 (0, 8)·(0, -8) 에 100 x 14(픽셀 y 37·53)다.
    void TestToggleGroupsDropdownsAndTransitions()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        Object::GameObject* tabs = stage.Make("tabs", {});
        auto* group = canvas->AttachComponent<Component::ToggleGroup2D>(tabs);
        Object::GameObject* first = stage.Make("first", {-60.0f, 30.0f}, tabs);
        auto* toggle1 = canvas->AttachComponent<Component::Toggle2D>(first);
        toggle1->size = {20.0f, 16.0f};
        auto* sprite1 = canvas->AttachComponent<Component::SpriteRenderer2D>(first);
        Object::GameObject* second = stage.Make("second", {0.0f, 30.0f}, tabs);
        auto* toggle2 = canvas->AttachComponent<Component::Toggle2D>(second);
        toggle2->size = {20.0f, 16.0f};
        Object::GameObject* third = stage.Make("third", {60.0f, 30.0f}, tabs);
        auto* toggle3 = canvas->AttachComponent<Component::Toggle2D>(third);
        toggle3->size = {20.0f, 16.0f};
        stage.Frames(2);
        Check(group->value == -1, "an empty group has no value");

        // 전이: 토글도 버튼처럼 호버·눌림·꺼짐 색을 대상 그림에 보인다.
        stage.Frame({MouseAt(40.0f, 20.0f)});
        Check(toggle1->hovered && CloseTo(sprite1->tint.R, 0.9f), "hovering a toggle tints its sprite like a button");
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(toggle1->pressed && CloseTo(sprite1->tint.R, 0.7f), "pressing shows the pressed tint");
        stage.Frame({Released(MouseButton::Left)});
        Check(toggle1->isOn && group->value == 0, "releasing turns the first toggle on and the group reports it");
        stage.Frame({MouseAt(100.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(toggle2->isOn && false == toggle1->isOn && group->value == 1, "turning on the second turns off the first");
        Check(CloseTo(sprite1->tint.R, 1.0f), "the first toggle's sprite is back to normal once the pointer left");
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(toggle2->isOn && false == toggle2->changed && group->value == 1, "pressing the on toggle again leaves it on while allowOff is false");
        group->allowOff = true;
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(false == toggle2->isOn && toggle2->changed && group->value == -1, "with allowOff the on toggle turns off");
        toggle1->isOn = true;
        toggle3->isOn = true;
        stage.Frame({});
        Check(toggle1->isOn && false == toggle3->isOn && group->value == 0, "two scripted on toggles settle to the first when neither was on");
        toggle3->isOn = true;
        stage.Frame({});
        Check(false == toggle1->isOn && toggle3->isOn && group->value == 2, "a newly turned-on toggle wins over the current one");
        toggle1->interactable = false;
        stage.Frame({MouseAt(40.0f, 20.0f)});
        Check(CloseTo(sprite1->tint.A, 0.6f), "a disabled toggle shows the disabled tint");
        toggle1->interactable = true;
        toggle1->transition = Component::ButtonTransition::None;
        sprite1->tint = {0.2f, 0.2f, 0.2f, 1.0f};
        stage.Frame({});
        Check(CloseTo(sprite1->tint.R, 0.2f), "with no transition the sprite's tint is left alone");
        stage.Frame({MouseAt(190.0f, 95.0f)});

        // 드롭다운: 누르면 목록이 펴지고, 항목을 고르면 값·캡션이 바뀌며 접힌다.
        g_hookCalls = 0;
        Object::GameObject* pick = stage.Make("pick", {0.0f, -40.0f});
        auto* dropdown = canvas->AttachComponent<Component::Dropdown2D>(pick);
        dropdown->size = {100.0f, 16.0f};
        auto* pickScript = canvas->AttachScript<SelfDestructor>(pick);
        (void)pickScript;
        Object::GameObject* cap = stage.Make("cap", {}, pick);
        auto* caption = canvas->AttachComponent<Component::Text2D>(cap);
        dropdown->caption = cap->GetScriptHandle().GetComponent<Handle::Text2D>();
        Object::GameObject* list = stage.Make("list", {0.0f, 45.0f}, pick);
        dropdown->list = list->GetScriptHandle();
        Object::GameObject* one = stage.Make("one", {0.0f, 8.0f}, list);
        auto* option1 = canvas->AttachComponent<Component::Toggle2D>(one);
        option1->size = {100.0f, 14.0f};
        auto* label1 = canvas->AttachComponent<Component::Text2D>(one);
        TextStore::Get().Assign(label1->text, "One", 3);
        Object::GameObject* two = stage.Make("two", {0.0f, -8.0f}, list);
        auto* option2 = canvas->AttachComponent<Component::Toggle2D>(two);
        option2->size = {100.0f, 14.0f};
        auto* label2 = canvas->AttachComponent<Component::Text2D>(two);
        TextStore::Get().Assign(label2->text, "Two", 3);
        stage.Frames(2);
        Check(false == list->IsActiveSelf() && false == dropdown->open && TextIs(caption->text, ""), "a closed dropdown hides its list and shows no caption");
        stage.Frame({MouseAt(100.0f, 53.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(false == option2->isOn && dropdown->value == -1, "a hidden option cannot be pressed");
        stage.Frame({MouseAt(100.0f, 90.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(dropdown->open && list->IsActiveSelf(), "pressing the dropdown opens its list");
        stage.Frame({MouseAt(100.0f, 53.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(dropdown->value == 1 && dropdown->changed && false == dropdown->open && g_hookCalls == 1, "picking the second option sets the value, closes and tells the script");
        stage.Frame({});
        Check(false == list->IsActiveSelf() && TextIs(caption->text, "Two") && option2->isOn && false == option1->isOn && false == dropdown->changed,
            "the caption shows the chosen label and only that option is on");
        stage.Frame({MouseAt(100.0f, 90.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(190.0f, 95.0f), HeldButton(MouseButton::Left)});
        Check(false == dropdown->open, "pressing outside closes it");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(100.0f, 90.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(dropdown->open, "it opens again");
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        Check(false == dropdown->open, "escape closes it");
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(100.0f, 53.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(dropdown->value == 1 && false == dropdown->changed && false == dropdown->open && g_hookCalls == 1, "picking the chosen option again closes without telling");
        dropdown->value = 0;
        stage.Frame({});
        Check(TextIs(caption->text, "One") && option1->isOn && false == option2->isOn, "a scripted value moves the caption and the options");
        dropdown->value = 7;
        stage.Frame({});
        Check(TextIs(caption->text, "") && false == option1->isOn && false == option2->isOn, "a value past the options clears the caption");
        dropdown->value = 0;
        stage.Frame({});
        TextStore::Get().Assign(label1->text, "Uno", 3);
        stage.Frame({});
        Check(TextIs(caption->text, "Uno"), "a renamed label reaches the caption");
        dropdown->open = true;
        stage.Frame({});
        Check(list->IsActiveSelf(), "a scripted open shows the list");
        dropdown->list = pick->GetScriptHandle();
        dropdown->open = false;
        stage.Frame({});
        Check(pick->IsActiveSelf() && list->IsActiveSelf(), "a list that is the dropdown itself is left alone");
    }

    // **드롭다운의 키보드·게임패드**(D-360). 드롭다운 `pick` 은 (0, -40) 100 x 16(픽셀 (100, 90)), 목록은 (0, 45) 아래 항목 둘(`one` y 8, `two` y -8) 이다.
    void TestDropdownsMoveByKeyboardAndGamepad()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        g_hookCalls = 0;
        Object::GameObject* pick = stage.Make("pick", {0.0f, -40.0f});
        auto* dropdown = canvas->AttachComponent<Component::Dropdown2D>(pick);
        dropdown->size = {100.0f, 16.0f};
        canvas->AttachScript<SelfDestructor>(pick);
        Object::GameObject* list = stage.Make("list", {0.0f, 45.0f}, pick);
        dropdown->list = list->GetScriptHandle();
        Object::GameObject* one = stage.Make("one", {0.0f, 8.0f}, list);
        auto* option1 = canvas->AttachComponent<Component::Toggle2D>(one);
        option1->size = {100.0f, 14.0f};
        auto* sprite1 = canvas->AttachComponent<Component::SpriteRenderer2D>(one);
        Object::GameObject* two = stage.Make("two", {0.0f, -8.0f}, list);
        auto* option2 = canvas->AttachComponent<Component::Toggle2D>(two);
        option2->size = {100.0f, 14.0f};
        auto* sprite2 = canvas->AttachComponent<Component::SpriteRenderer2D>(two);
        stage.Frames(2);
        Check(dropdown->highlighted == -1, "a closed dropdown highlights nothing");
        // 키보드: 누르면 펴지고 첫 항목이 돋아 있다. ↓ 가 둘째로, Enter 가 고른다.
        stage.Frame({MouseAt(100.0f, 90.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(190.0f, 2.0f)});
        Check(dropdown->open && dropdown->highlighted == 0 && option1->hovered && CloseTo(sprite1->tint.R, 0.9f),
            "opening with no value highlights the first option and it looks hovered");
        stage.Frame({Pressed(Key::Down)});
        stage.Frame({ReleasedKey(Key::Down)});
        Check(dropdown->highlighted == 1 && option2->hovered && false == option1->hovered && CloseTo(sprite2->tint.R, 0.9f) && CloseTo(sprite1->tint.R, 1.0f),
            "down moves the highlight to the second option");
        stage.Frame({Pressed(Key::Down)});
        stage.Frame({ReleasedKey(Key::Down)});
        Check(dropdown->highlighted == 1, "down at the last option stays");
        stage.Frame({Pressed(Key::Enter)});
        Check(dropdown->value == 1 && dropdown->changed && false == dropdown->open && dropdown->highlighted == -1 && g_hookCalls == 1,
            "enter picks the highlighted option, closes and tells the script");
        stage.Frame({ReleasedKey(Key::Enter)});
        Check(false == option2->hovered && CloseTo(sprite2->tint.R, 1.0f), "once closed nothing looks hovered");
        stage.Frame({Pressed(Key::Enter)});
        stage.Frame({ReleasedKey(Key::Enter)});
        Check(dropdown->open && dropdown->highlighted == 1, "enter on a closed dropdown opens it at the chosen option");
        stage.Frame({Pressed(Key::Up)});
        stage.Frame({ReleasedKey(Key::Up)});
        stage.Frame({Pressed(Key::Up)});
        stage.Frame({ReleasedKey(Key::Up)});
        Check(dropdown->highlighted == 0, "up stops at the first option");
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        Check(false == dropdown->open && dropdown->highlighted == -1 && dropdown->value == 1, "escape closes without picking");
        // 쥐면 되풀이: Space 로 펴고 ↓ 를 쥐면 0.4 초 뒤 0.1 초마다 옮긴다 - 항목이 둘이라 끝에서 멈춘다.
        stage.Frame({Pressed(Key::Space)});
        stage.Frame({ReleasedKey(Key::Space)});
        dropdown->value = -1;
        stage.Frame({Pressed(Key::Up)});
        Check(dropdown->open && dropdown->highlighted == 0, "space opens and up from the top stays at the top");
        stage.Frame({ReleasedKey(Key::Up)});
        stage.Frame({Pressed(Key::Down)});
        stage.Frames(30);
        Check(dropdown->highlighted == 1, "a held down key repeats and stops at the end");
        stage.Frame({ReleasedKey(Key::Down)});
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        // 게임패드: South 가 펴고, 십자키가 돋움을 옮기며, South 가 고른다. 좌우는 포커스를 옮기지 않는다.
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(dropdown->open && dropdown->highlighted == 0, "the south button opens it at the first option");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadDown);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(dropdown->highlighted == 1 && option2->hovered, "the d-pad moves the highlight");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadLeft);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(dropdown->open && dropdown->focused && dropdown->highlighted == 1, "left and right do nothing while the list is open");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(dropdown->value == 1 && false == dropdown->open && g_hookCalls == 2, "south picks the highlighted option");
        // 항목이 없으면 돋움도 없고 Enter 는 접기만 한다. 목록이 사라지면 -1 이다.
        dropdown->list = {};
        stage.Frame({Pressed(Key::Enter)});
        stage.Frame({ReleasedKey(Key::Enter)});
        Check(dropdown->open && dropdown->highlighted == -1, "with no list it opens with nothing highlighted");
        stage.Frame({Pressed(Key::Down)});
        stage.Frame({ReleasedKey(Key::Down)});
        stage.Frame({Pressed(Key::Enter)});
        stage.Frame({ReleasedKey(Key::Enter)});
        Check(false == dropdown->open && dropdown->value == 1 && g_hookCalls == 2, "enter with nothing highlighted only closes");
        dropdown->list = list->GetScriptHandle();
        dropdown->value = 9;
        stage.Frame({Pressed(Key::Enter)});
        stage.Frame({ReleasedKey(Key::Enter)});
        Check(dropdown->open && dropdown->highlighted == 0, "a value past the options highlights the first");
        dropdown->interactable = false;
        stage.Frame({Pressed(Key::Down)});
        stage.Frame({ReleasedKey(Key::Down)});
        Check(dropdown->highlighted == 0, "a disabled dropdown ignores the keys");
    }

    // **스크롤바**(D-355). 창(마스크 100 x 60)이 가운데, 내용(100 x 200)은 위가 맞춰져(-70) 있고, 세로 막대는 (60, 0) 의 10 x 60 슬라이더(위에서 아래로)다.
    void TestScrollbarsFollowTheContent()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        Object::GameObject* window = stage.Make("window", {});
        auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
        mask->size = {100.0f, 60.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        scroll->contentSize = {100.0f, 200.0f};
        Object::GameObject* content = stage.Make("content", {0.0f, -70.0f}, window);
        scroll->content = content->GetScriptHandle();
        auto* contentPlace = stage.PlaceOf(content);
        Object::GameObject* bar = stage.Make("bar", {60.0f, 0.0f});
        auto* slider = canvas->AttachComponent<Component::Slider2D>(bar);
        slider->direction = Component::SliderDirection::TopToBottom;
        slider->size = {10.0f, 60.0f};
        Object::GameObject* knob = stage.Make("knob", {}, bar);
        auto* knobSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(knob);
        knobSprite->sizeMode = Component::SpriteSizeMode::Custom;
        knobSprite->size = {10.0f, 10.0f};
        slider->handle = knob->GetScriptHandle();
        scroll->verticalBar = bar->GetScriptHandle();
        stage.Frames(2);
        Check(CloseTo(slider->value, 0.0f) && CloseTo(knobSprite->size.y, 18.0f) && CloseTo(stage.PlaceOf(knob)->position.y, 30.0f),
            "with the content at the top the bar reads zero and the knob is the viewport's share of the track");
        // 휠로 내용이 40 내려가면 막대가 따라온다(140 가운데 40).
        stage.Frame({MouseAt(100.0f, 50.0f), Wheel(-1.0f)});
        Check(CloseTo(contentPlace->position.y, -30.0f) && CloseTo(slider->value, 40.0f / 140.0f), "scrolling the content moves the bar");
        // 손잡이 밖의 트랙을 누르면 한 페이지(뷰포트 60 / 남은 140 = 0.4286)씩이다(D-360): 막대 가운데(픽셀 (160, 50))는 0.5, 손잡이(0.2857 ± 0.15) 밖 → 0.7143, 내용 30.
        stage.Frame({MouseAt(160.0f, 50.0f), HeldButton(MouseButton::Left)});
        Check(CloseTo(slider->value, 40.0f / 140.0f + 60.0f / 140.0f) && CloseTo(contentPlace->position.y, 30.0f), "pressing the track below the knob pages the content down by one viewport");
        stage.Frames(5);
        Check(CloseTo(slider->value, 100.0f / 140.0f), "having passed the pointer holding does not page again");
        stage.Frame({Released(MouseButton::Left)});
        // 손잡이 위(0.7143 → 로컬 y -12.9, 픽셀 y 62.9)를 누르면 끌기다: 포인터의 자리가 값이다.
        stage.Frame({MouseAt(160.0f, 63.0f), HeldButton(MouseButton::Left)});
        Check(CloseTo(slider->value, 43.0f / 60.0f) && CloseTo(contentPlace->position.y, -70.0f + 140.0f * 43.0f / 60.0f), "pressing on the knob drags it");
        stage.Frame({MouseAt(160.0f, 80.0f)});
        Check(CloseTo(slider->value, 1.0f) && CloseTo(contentPlace->position.y, 70.0f), "the bottom of the bar is the bottom of the content");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frames(2);
        Check(CloseTo(slider->value, 1.0f) && CloseTo(contentPlace->position.y, 70.0f), "letting go keeps both where they are");
        // 트랙 위쪽(픽셀 y 25 → 값 0.0833)을 쥐면 0.4 초 뒤 0.1 초마다 한 페이지씩 - 1 → 0.5714 → 0.1429 에서 손잡이가 포인터에 닿아 멈춘다.
        stage.Frame({MouseAt(160.0f, 25.0f), HeldButton(MouseButton::Left)});
        Check(CloseTo(slider->value, 1.0f - 60.0f / 140.0f) && CloseTo(contentPlace->position.y, 10.0f), "pressing the track above the knob pages up");
        stage.Frames(23);
        Check(CloseTo(slider->value, 1.0f - 60.0f / 140.0f), "before the repeat delay nothing more happens");
        stage.Frame({});
        Check(CloseTo(slider->value, 1.0f - 120.0f / 140.0f) && CloseTo(contentPlace->position.y, -50.0f), "after the delay it pages once more");
        stage.Frames(12);
        Check(CloseTo(slider->value, 1.0f - 120.0f / 140.0f), "once the knob reaches the pointer the repeat stops");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(160.0f, 78.0f), HeldButton(MouseButton::Left)});
        stage.Frames(60);
        Check(CloseTo(slider->value, 1.0f) && CloseTo(contentPlace->position.y, 70.0f), "paging past the end clamps at the end");
        stage.Frame({Released(MouseButton::Left)});
        // 가운데(값 0.5)를 누르면 1 → 0.5714 에서 손잡이가 포인터를 덮어 멈추고, 쥔 채 포인터를 더 위로(y 25, 값 0.0833) 옮기면 다시 한 페이지(0.1429)다. 반대쪽으로 옮기면 그대로다.
        stage.Frame({MouseAt(160.0f, 50.0f), HeldButton(MouseButton::Left)});
        stage.Frames(30);
        Check(CloseTo(slider->value, 1.0f - 60.0f / 140.0f), "a page that lands on the pointer stops there");
        stage.Frame({MouseAt(160.0f, 25.0f)});
        stage.Frames(7);
        Check(CloseTo(slider->value, 1.0f - 120.0f / 140.0f) && CloseTo(contentPlace->position.y, -50.0f), "moving the held pointer further pages again");
        stage.Frame({MouseAt(160.0f, 70.0f)});
        stage.Frames(7);
        Check(CloseTo(slider->value, 1.0f - 120.0f / 140.0f), "moving the held pointer to the other side of the knob does nothing");
        stage.Frame({Released(MouseButton::Left)});
        // 스크롤 뷰의 막대가 아닌 슬라이더는 전처럼 포인터 자리로 뛴다.
        scroll->verticalBar = {};
        stage.Frame({MouseAt(160.0f, 50.0f), HeldButton(MouseButton::Left)});
        Check(CloseTo(slider->value, 0.5f), "a plain slider still jumps to the pointer");
        stage.Frame({Released(MouseButton::Left)});
        scroll->verticalBar = bar->GetScriptHandle();
        slider->value = 1.0f;
        stage.Frame({});
        // 내용이 뷰포트보다 작으면 막대는 0 이고 손잡이가 트랙 전체다.
        scroll->contentSize = {100.0f, 30.0f};
        stage.Frame({});
        Check(CloseTo(slider->value, 0.0f) && CloseTo(knobSprite->size.y, 60.0f), "content smaller than the viewport parks the bar at zero with a full-length knob");
        // 가로 막대는 가로 스크롤이 꺼져 있으면 0 이다. NaN 내용 크기는 자리를 흔들지 않는다.
        scroll->verticalBar = {};
        scroll->horizontalBar = bar->GetScriptHandle();
        scroll->contentSize = {NaN, 200.0f};
        stage.Frame({});
        Check(Finite(contentPlace->position) && CloseTo(slider->value, 0.0f), "a disabled axis and a NaN content size leave the bar at zero and the content finite");
        scroll->horizontalBar = {};
        scroll->verticalBar = bar->GetScriptHandle();
        scroll->contentSize = {100.0f, 200.0f};
        // 막대가 자기 자신이나 내용이 자식이 아니면 아무것도 하지 않는다.
        scroll->content = window->GetScriptHandle();
        slider->value = 0.3f;
        stage.Frame({});
        Check(CloseTo(slider->value, 0.3f), "a content that is the window itself leaves the bar alone");
    }

    // **툴팁·모달·탭·끌어 놓기**(D-357, B 2 순위). 화면 200 x 100 = 기준. 툴팁 `help` 는 (0, 0) 40 x 20(픽셀 (100, 50)), 모달 밖 버튼 `under` 는 (-60, -30)(픽셀 (40, 80)),
    // 모달 안 버튼 `ok` 는 (60, -30)(픽셀 (160, 80)), 탭 토글은 (-60, 30)·(0, 30), 끌 것 `card` 는 (-20, -30)(픽셀 (80, 80)) 20 x 20, 놓을 자리 `slot` 은 (30, -30)(픽셀 (130, 80)) 30 x 30 이다.
    void TestTooltipsModalsTabsAndDragging()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));

        // 툴팁: 0.2 초(12 프레임) 머물면 대상이 켜지고, 누르거나 떠나면 꺼진다.
        Object::GameObject* help = stage.Make("help", {});
        auto* tooltip = canvas->AttachComponent<Component::Tooltip2D>(help);
        tooltip->size = {40.0f, 20.0f};
        tooltip->delay = 0.2f;
        Object::GameObject* tip = stage.Make("tip", {0.0f, 20.0f}, help);
        tooltip->target = tip->GetScriptHandle();
        stage.Frames(2);
        Check(false == tip->IsActiveSelf() && false == tooltip->shown, "the tooltip target starts hidden");
        stage.Frame({MouseAt(100.0f, 50.0f)});
        stage.Frames(5);
        Check(false == tooltip->shown, "hovering for less than the delay shows nothing");
        stage.Frames(10);
        Check(tooltip->shown && tip->IsActiveSelf(), "after the delay the target shows");
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(false == tooltip->shown && false == tip->IsActiveSelf() && poller->sawMouse, "pressing hides it and the press reaches the game");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(190.0f, 95.0f)});
        stage.Frames(15);
        Check(false == tooltip->shown, "away from it nothing shows");
        tooltip->target = help->GetScriptHandle();
        stage.Frame({MouseAt(100.0f, 50.0f)});
        stage.Frames(15);
        Check(help->IsActiveSelf() && tooltip->shown, "a target that is the tooltip itself is left alone");
        tooltip->target = tip->GetScriptHandle();
        tooltip->delay = NaN;
        stage.Frame({MouseAt(190.0f, 95.0f)});
        stage.Frame({MouseAt(100.0f, 50.0f)});
        Check(tooltip->shown, "a NaN delay shows at once");
        // 따라가기(D-360): 대상은 포인터 + (12, -12) 에 있고 포인터를 따라 움직인다. 끄면 제자리다.
        Check(CloseTo(stage.PlaceOf(tip)->position.y, 20.0f), "without follow the target stays where it was put");
        tooltip->follow = true;
        stage.Frame({MouseAt(100.0f, 50.0f)});
        Check(tooltip->shown && CloseTo(stage.PlaceOf(tip)->position.x, 12.0f) && CloseTo(stage.PlaceOf(tip)->position.y, -12.0f), "with follow the target sits at the pointer plus the offset");
        stage.Frame({MouseAt(110.0f, 42.0f)});
        Check(tooltip->shown && CloseTo(stage.PlaceOf(tip)->position.x, 22.0f) && CloseTo(stage.PlaceOf(tip)->position.y, -4.0f), "it follows the pointer while shown");
        tooltip->followOffset = {NaN, 0.0f};
        stage.Frame({MouseAt(112.0f, 42.0f)});
        Check(CloseTo(stage.PlaceOf(tip)->position.x, 22.0f), "a NaN offset leaves the target where it was");
        tooltip->followOffset = {12.0f, -12.0f};
        tooltip->follow = false;
        tooltip->delay = 0.2f;
        stage.Frame({MouseAt(190.0f, 95.0f)});

        // 모달: 켜져 있으면 안의 위젯만 받고 게임은 아무것도 보지 못한다.
        Object::GameObject* under = stage.Make("under", {-60.0f, -30.0f});
        auto* underButton = canvas->AttachComponent<Component::Button2D>(under);
        underButton->size = {30.0f, 20.0f};
        Object::GameObject* dialog = stage.Make("dialog", {});
        auto* modal = canvas->AttachComponent<Component::Modal2D>(dialog);
        Object::GameObject* ok = stage.Make("ok", {60.0f, -30.0f}, dialog);
        auto* okButton = canvas->AttachComponent<Component::Button2D>(ok);
        okButton->size = {30.0f, 20.0f};
        stage.Frames(2);
        stage.Frame({MouseAt(40.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(false == underButton->clicked && false == underButton->hovered && false == poller->sawMouse, "with a modal up a button outside it is neither hovered nor clicked and the game sees no mouse");
        stage.Frame({MouseAt(160.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(okButton->clicked, "a button inside the modal works");
        stage.Frame({MouseAt(100.0f, 50.0f)});
        stage.Frames(15);
        Check(false == tooltip->shown, "a tooltip outside the modal does not show");
        stage.Frame({MouseAt(190.0f, 95.0f)});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(okButton->focused && false == underButton->focused, "gamepad navigation only lands inside the modal");
        modal->blockGame = false;
        stage.Frame({MouseAt(40.0f, 80.0f), HeldButton(MouseButton::Left)});
        Check(poller->sawMouse && false == underButton->pressed, "with blockGame off the game sees the mouse but the outside button still does not");
        stage.Frame({Released(MouseButton::Left)});
        dialog->SetActive(false);
        stage.Frame({MouseAt(40.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(underButton->clicked, "closing the modal frees the outside button");

        // 탭: 그룹이 고른 번호의 페이지만 켜진다.
        Object::GameObject* tabs = stage.Make("tabs", {});
        canvas->AttachComponent<Component::ToggleGroup2D>(tabs);
        auto* tabGroup = canvas->AttachComponent<Component::TabGroup2D>(tabs);
        Object::GameObject* pages = stage.Make("pages", {}, tabs);
        tabGroup->content = pages->GetScriptHandle();
        Object::GameObject* first = stage.Make("first", {-60.0f, 30.0f}, tabs);
        canvas->AttachComponent<Component::Toggle2D>(first)->size = {20.0f, 16.0f};
        Object::GameObject* second = stage.Make("second", {0.0f, 30.0f}, tabs);
        canvas->AttachComponent<Component::Toggle2D>(second)->size = {20.0f, 16.0f};
        Object::GameObject* pageOne = stage.Make("pageOne", {}, pages);
        Object::GameObject* pageTwo = stage.Make("pageTwo", {}, pages);
        stage.Frames(2);
        Check(false == pageOne->IsActiveSelf() && false == pageTwo->IsActiveSelf() && tabGroup->page == -1, "no tab chosen hides every page");
        stage.Frame({MouseAt(100.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(pageTwo->IsActiveSelf() && false == pageOne->IsActiveSelf() && tabGroup->page == 1, "choosing the second tab shows the second page");
        stage.Frame({MouseAt(40.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(pageOne->IsActiveSelf() && false == pageTwo->IsActiveSelf() && tabGroup->page == 0, "choosing the first tab switches the page");
        tabGroup->content = tabs->GetScriptHandle();
        stage.Frame({});
        Check(tabs->IsActiveSelf() && first->IsActiveSelf() && second->IsActiveSelf(), "a content that is the tab group itself is left alone");
        tabGroup->content = pages->GetScriptHandle();

        // 끌어 놓기.
        Object::GameObject* card = stage.Make("card", {-20.0f, -30.0f});
        auto* draggable = canvas->AttachComponent<Component::Draggable2D>(card);
        draggable->size = {20.0f, 20.0f};
        auto* cardSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(card);
        cardSprite->renderOrder = 3;
        Object::GameObject* cardLabel = stage.Make("cardLabel", {}, card);
        auto* cardText = canvas->AttachComponent<Component::Text2D>(cardLabel);
        auto* cardProbe = canvas->AttachScript<DragProbe>(card);
        Object::GameObject* slot = stage.Make("slot", {30.0f, -30.0f});
        auto* target = canvas->AttachComponent<Component::DropTarget2D>(slot);
        target->size = {30.0f, 30.0f};
        auto* slotProbe = canvas->AttachScript<DragProbe>(slot);
        stage.Frames(2);
        stage.Frame({MouseAt(80.0f, 80.0f), HeldButton(MouseButton::Left)});
        Check(draggable->dragging && cardProbe->begins == 1 && false == poller->sawMouse, "pressing a draggable starts a drag and takes the pointer");
        Check(cardSprite->renderOrder == 103 && cardText->renderOrder == 100 && draggable->lifted == 100, "while dragging the sprite and the child text are lifted by liftOrder");
        stage.Frame({MouseAt(105.0f, 80.0f)});
        Check(CloseTo(stage.PlaceOf(card)->position.x, 5.0f) && false == target->hovering, "the object follows the pointer");
        stage.Frame({MouseAt(130.0f, 80.0f)});
        Check(CloseTo(stage.PlaceOf(card)->position.x, 30.0f) && target->hovering, "over the slot the target reports hovering");
        stage.Frame({Released(MouseButton::Left)});
        Check(target->dropped.GetInstanceId() == card->GetInstanceId() && draggable->droppedOn.GetInstanceId() == slot->GetInstanceId()
                && slotProbe->drops == 1 && cardProbe->ends == 1 && false == draggable->dragging,
            "releasing drops it on the slot and tells both scripts");
        Check(cardSprite->renderOrder == 3 && cardText->renderOrder == 0 && draggable->lifted == 0, "dropping puts the render orders back");
        // 끌리는 동안 붙인 자식 그림은 놓을 때 내려가지 않는다(D-361). 끌리는 동안 뗀 자식은 그대로 둔다. 빈 곳에 놓아 제자리로 돌아온다.
        draggable->snapBack = true;
        stage.Frame({HeldButton(MouseButton::Left)});
        Object::GameObject* badge = stage.Make("badge", {}, card);
        auto* badgeSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(badge);
        badgeSprite->renderOrder = 5;
        stage.Frame({MouseAt(170.0f, 95.0f)});
        Check(draggable->dragging && badgeSprite->renderOrder == 5 && cardSprite->renderOrder == 103, "a sprite attached mid-drag is not lifted");
        cardLabel->SetParent(nullptr);
        stage.Frame({Released(MouseButton::Left)});
        Check(badgeSprite->renderOrder == 5 && cardSprite->renderOrder == 3 && cardText->renderOrder == 0, "dropping lowers only what was lifted, including a child moved away meanwhile");
        Check(CloseTo(stage.PlaceOf(card)->position.x, 30.0f) && cardProbe->ends == 2, "and it snapped back");
        draggable->snapBack = false;
        cardLabel->SetParent(card);
        canvas->DestroyObject(badge);
        stage.Frame({MouseAt(130.0f, 80.0f)});
        stage.Frame({});
        Check(false == target->dropped.IsValid() && false == draggable->droppedOn.IsValid() && false == target->hovering, "the drop lasts one frame");
        draggable->snapBack = true;
        draggable->liftOrder = 0;
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(draggable->dragging && cardSprite->renderOrder == 3 && draggable->lifted == 0, "a lift of zero leaves the orders alone");
        draggable->liftOrder = 7;
        stage.Frame({MouseAt(170.0f, 95.0f)});
        Check(CloseTo(stage.PlaceOf(card)->position.x, 70.0f) && CloseTo(stage.PlaceOf(card)->position.y, -45.0f), "dragging again moves it");
        stage.Frame({Released(MouseButton::Left)});
        Check(cardSprite->renderOrder == 3, "changing liftOrder mid-drag does not change what is taken back");
        draggable->liftOrder = 100;
        Check(CloseTo(stage.PlaceOf(card)->position.x, 30.0f) && CloseTo(stage.PlaceOf(card)->position.y, -30.0f) && slotProbe->drops == 1 && cardProbe->ends == 3,
            "released over nothing it snaps back");
        // 놓는 훅에서 제 오브젝트를 지워도 다음 누름은 게임에 간다.
        cardProbe->destroyOnEnd = true;
        stage.Frame({MouseAt(130.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({MouseAt(150.0f, 80.0f)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({});
        stage.Frame({MouseAt(150.0f, 80.0f), HeldButton(MouseButton::Left)});
        Check(poller->sawMouse, "after the dragged object destroyed itself the next press reaches the game");
        stage.Frame({Released(MouseButton::Left)});
    }

    // **안전 영역·그룹·가상 키보드·탐색 연결**(D-358, B 3 순위). 화면 200 x 100 = 기준(띠는 대상 픽셀 = 기준 픽셀). 글자 칸은 (-40, -30) 60 x 20(픽셀 (60, 80)),
    // 키는 y 30(픽셀 y 20)에 (-60)·(0)·(60), 그룹 아래 버튼은 (60, -30)(픽셀 (160, 80)), 연결 시험의 버튼은 (0, 45)·(-80, -45)·(80, -45) 다.
    void TestSafeAreaGroupsKeyboardAndLinks()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        auto* poller = canvas->AttachScript<GamePoller>(canvas->CreateObject("poller"));

        // 안전 영역: 왼쪽 20·위 10 의 띠. 왼쪽 위 앵커의 루트는 안으로 들어온다.
        ScreenSpaceFrame frame;
        frame.referenceWidth = 200.0f;
        frame.referenceHeight = 100.0f;
        frame.targetWidth = 200.0f;
        frame.targetHeight = 100.0f;
        frame.safeArea.left = 20.0f;
        frame.safeArea.top = 10.0f;
        stage.framework.SetScreenSpace(frame);
        Object::GameObject* corner = stage.Make("corner", {});
        stage.PlaceOf(corner)->anchor = {0.0f, 1.0f};
        auto* safe = canvas->AttachComponent<Component::SafeArea2D>(corner);
        Object::GameObject* plain = stage.Make("plain", {});
        stage.PlaceOf(plain)->anchor = {0.0f, 1.0f};
        stage.Frames(2);
        Check(CloseTo(stage.PlaceOf(corner)->worldPosition.x, -80.0f) && CloseTo(stage.PlaceOf(corner)->worldPosition.y, 40.0f), "a safe-area root anchors inside the insets");
        Check(CloseTo(stage.PlaceOf(plain)->worldPosition.x, -100.0f) && CloseTo(stage.PlaceOf(plain)->worldPosition.y, 50.0f), "a plain root anchors at the screen corner");
        safe->horizontal = false;
        stage.Frame({});
        Check(CloseTo(stage.PlaceOf(corner)->worldPosition.x, -100.0f) && CloseTo(stage.PlaceOf(corner)->worldPosition.y, 40.0f), "a disabled axis uses the full screen on that axis");
        frame.safeArea = {};
        stage.framework.SetScreenSpace(frame);
        stage.Frame({});
        Check(CloseTo(stage.PlaceOf(corner)->worldPosition.x, -100.0f) && CloseTo(stage.PlaceOf(corner)->worldPosition.y, 50.0f), "without insets the safe area is the screen");

        // 그룹: 알파는 곱이고, interactable 이 거짓이면 아래 버튼이 눌리지 않는다.
        Object::GameObject* panel = stage.Make("panel", {});
        auto* group = canvas->AttachComponent<Component::CanvasGroup2D>(panel);
        group->alpha = 0.5f;
        Object::GameObject* inner = stage.Make("inner", {}, panel);
        auto* innerGroup = canvas->AttachComponent<Component::CanvasGroup2D>(inner);
        innerGroup->alpha = 0.5f;
        Object::GameObject* press = stage.Make("press", {60.0f, -30.0f}, inner);
        auto* pressButton = canvas->AttachComponent<Component::Button2D>(press);
        pressButton->size = {30.0f, 20.0f};
        Check(CloseTo(ComputeGroupAlpha2D(*canvas, press), 0.25f) && CloseTo(ComputeGroupAlpha2D(*canvas, panel), 0.5f) && CloseTo(ComputeGroupAlpha2D(*canvas, corner), 1.0f),
            "nested group alphas multiply and an object outside any group is opaque");
        innerGroup->alpha = NaN;
        Check(CloseTo(ComputeGroupAlpha2D(*canvas, press), 0.5f), "a NaN alpha counts as one");
        stage.Frames(2);
        stage.Frame({MouseAt(160.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(pressButton->clicked, "a button under interactable groups clicks");
        group->interactable = false;
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(false == pressButton->pressed && false == pressButton->hovered && poller->sawMouse && false == IsGroupInteractable2D(*canvas, press),
            "a button under a non-interactable group is nothing and the press reaches the game");
        stage.Frame({Released(MouseButton::Left)});
        group->interactable = true;
        // blocksRaycasts(D-361): 거짓이면 포인터만 지나간다 - 게임패드 포커스와 누름은 그대로다.
        group->blocksRaycasts = false;
        stage.Frame({HeldButton(MouseButton::Left)});
        Check(false == pressButton->pressed && false == pressButton->hovered && poller->sawMouse && IsGroupInteractable2D(*canvas, press)
                && false == IsGroupBlockingRaycasts2D(*canvas, press),
            "a button under a group that does not block raycasts is passed over by the pointer");
        stage.Frame({Released(MouseButton::Left)});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(pressButton->focused, "but the gamepad still reaches it");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(pressButton->clicked, "and clicks it");
        group->blocksRaycasts = true;
        stage.Frame({MouseAt(190.0f, 2.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(false == pressButton->focused, "pressing empty space drops the focus again");

        // 가상 키보드: 키 버튼은 포커스를 가져가지 않고 글자를 칸에 넣는다.
        Object::GameObject* board = stage.Make("board", {});
        auto* keyboard = canvas->AttachComponent<Component::VirtualKeyboard2D>(board);
        Object::GameObject* keyA = stage.Make("keyA", {-60.0f, 30.0f}, board);
        canvas->AttachComponent<Component::Button2D>(keyA)->size = {20.0f, 16.0f};
        auto* labelA = canvas->AttachComponent<Component::Text2D>(keyA);
        TextStore::Get().Assign(labelA->text, "A", 1);
        Object::GameObject* back = stage.Make("back", {0.0f, 30.0f}, board);
        canvas->AttachComponent<Component::Button2D>(back)->size = {20.0f, 16.0f};
        keyboard->backspace = back->GetScriptHandle();
        Object::GameObject* enter = stage.Make("enter", {60.0f, 30.0f}, board);
        canvas->AttachComponent<Component::Button2D>(enter)->size = {20.0f, 16.0f};
        keyboard->enter = enter->GetScriptHandle();
        Object::GameObject* name = stage.Make("name", {-40.0f, -30.0f});
        auto* field = canvas->AttachComponent<Component::InputField2D>(name);
        field->size = {60.0f, 20.0f};
        stage.Frames(2);
        stage.Frame({MouseAt(60.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->focused, "the field takes focus");
        stage.Frame({MouseAt(40.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "A") && field->focused && field->caretIndex == 1, "a virtual key types into the focused field and leaves its focus alone");
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "AA"), "and again");
        stage.Frame({MouseAt(100.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "A") && field->caretIndex == 1, "the backspace key erases before the caret");
        stage.Frame({MouseAt(160.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(field->submitted, "the enter key submits");
        stage.Frame({Pressed(Key::Escape)});
        stage.Frame({ReleasedKey(Key::Escape)});
        stage.Frame({MouseAt(40.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "A") && false == field->focused, "without a focused field a key types nowhere");
        keyboard->target = name->GetScriptHandle();
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "AA"), "a keyboard with a target types into it without focus");
        field->maxLength = 2;
        stage.Frame({HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "AA"), "the field's limit holds for virtual keys too");
        keyboard->target = {};
        // 게임패드(D-360): 칸에 포커스를 두고 십자키로 키에 가서 South 를 누르면 마지막 포커스 칸에 들어간다. East 는 지우기, Enter 키는 제출이다.
        field->maxLength = 0;
        field->submitted = false;
        stage.Frame({MouseAt(60.0f, 80.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(190.0f, 2.0f)});
        Check(field->focused && field->caretIndex == 2, "the field is focused again with the caret at the end");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadUp);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        for (JBro::Int32 step = 0; step < 2; ++step)
        {
            stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadLeft);
            stage.Frame({});
            stage.platform.pads[0].buttons = 0;
            stage.Frame({});
        }
        auto* keyAButton = canvas->FindComponentRaw<Component::Button2D>(keyA);
        Check(keyAButton->focused && false == field->focused, "the d-pad walks from the field onto the left-most key");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(keyAButton->clicked && keyAButton->focused, "south on a key is a click and the key keeps focus");
        Check(TextIs(field->text, "AAA"), "south on a key types into the last focused field");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::East);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(TextIs(field->text, "AA"), "east erases before the caret");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadRight);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::South);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(canvas->FindComponentRaw<Component::Button2D>(enter)->focused && field->submitted, "two steps right is the enter key and south submits");
        // 마지막 포커스 칸이 사라지면 아무 데도 넣지 않는다. 포인터로 누른 키는 전처럼 포커스 칸이 있어야 한다.
        name->SetActive(false);
        stage.platform.pads[0].buttons = PadBit(GamepadButton::East);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        name->SetActive(true);
        stage.Frame({});
        Check(TextIs(field->text, "AA"), "a hidden last field is left alone");
        stage.Frame({MouseAt(40.0f, 20.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(TextIs(field->text, "AA"), "a pointer press on a key still needs a focused field");

        // 탐색 연결: 맨 위의 버튼이 먼저 포커스를 받고, 그 `down` 연결이 방향 탐색 대신 간다. 꺼진 위젯을 가리키면 그 방향은 없다.
        Object::GameObject* top = stage.Make("top", {0.0f, 45.0f});
        auto* topButton = canvas->AttachComponent<Component::Button2D>(top);
        topButton->size = {20.0f, 8.0f};
        auto* links = canvas->AttachComponent<Component::Navigation2D>(top);
        Object::GameObject* farLeft = stage.Make("farLeft", {-80.0f, -45.0f});
        auto* farLeftButton = canvas->AttachComponent<Component::Button2D>(farLeft);
        farLeftButton->size = {20.0f, 8.0f};
        Object::GameObject* farRight = stage.Make("farRight", {80.0f, -45.0f});
        auto* farRightButton = canvas->AttachComponent<Component::Button2D>(farRight);
        farRightButton->size = {20.0f, 8.0f};
        links->down = farRight->GetScriptHandle();
        stage.Frame({MouseAt(190.0f, 2.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadDown);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(topButton->focused, "with no focus the top-most widget is picked");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadDown);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(farRightButton->focused && false == topButton->focused, "an explicit down link wins over the direction search");
        links->down = farLeft->GetScriptHandle();
        farLeftButton->interactable = false;
        stage.Frame({MouseAt(100.0f, 5.0f), HeldButton(MouseButton::Left)});
        stage.Frame({Released(MouseButton::Left)});
        Check(topButton->focused, "pressing the top button focuses it again");
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadDown);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(topButton->focused && false == farLeftButton->focused, "a link to a disabled widget goes nowhere");
        links->down = top->GetScriptHandle();
        stage.platform.pads[0].buttons = PadBit(GamepadButton::DPadDown);
        stage.Frame({});
        stage.platform.pads[0].buttons = 0;
        stage.Frame({});
        Check(topButton->focused, "a link to itself goes nowhere");
    }

    // **분할 화면의 포인터**(D-359). 왼쪽 절반의 카메라 A(월드 (-10, 0), 반높이 50)와 오른쪽 절반의 카메라 B((10, 0)). 왼쪽 절반의 가운데 픽셀은 A 로 (-10, 0),
    // 오른쪽 절반의 가운데는 B 로 (10, 0) 이고, 월드 버튼은 제 카메라의 절반에서만 눌린다.
    void TestSplitCamerasMapThePointer()
    {
        Stage stage;
        Canvas* canvas = stage.canvas;
        const auto makeCamera = [&](const char* name, JBro::Float x, JBro::Float originX, JBro::Bool primary) {
            Object::GameObject* object = canvas->CreateObject(name);
            canvas->AttachComponent<Component::Transform2D>(object)->position = {x, 0.0f};
            auto* camera = canvas->AttachComponent<Component::Camera2D>(object);
            camera->primary = primary;
            camera->orthographicSize = 50.0f;
            camera->viewportOrigin = {originX, 0.0f};
            camera->viewportSize = {0.5f, 1.0f};
            return camera;
        };
        auto* left = makeCamera("left", -10.0f, 0.0f, true);
        makeCamera("right", 10.0f, 0.5f, false);
        Object::GameObject* world = canvas->CreateObject("world");
        canvas->AttachComponent<Component::Transform2D>(world)->position = {10.0f, 0.0f};
        auto* button = canvas->AttachComponent<Component::Button2D>(world);
        button->size = {20.0f, 20.0f};
        stage.Frames(2);
        const Service::Screen2DService& screen = GetFramework2DServices().Screen2D;
        Vector2 point;
        Check(screen.ScreenToLayer({50.0f, 50.0f}, world->GetScriptHandle(), point) && CloseTo(point.x, -10.0f) && CloseTo(point.y, 0.0f),
            "the middle of the left half maps through the left camera");
        Check(screen.ScreenToLayer({150.0f, 50.0f}, world->GetScriptHandle(), point) && CloseTo(point.x, 10.0f) && CloseTo(point.y, 0.0f),
            "the middle of the right half maps through the right camera");
        Check(screen.ScreenToLayer({175.0f, 25.0f}, world->GetScriptHandle(), point) && CloseTo(point.x, 35.0f) && CloseTo(point.y, 25.0f),
            "a quarter into the right half is a quarter of the camera's width");
        // 버튼은 (10, 0) - 오른쪽 절반의 가운데에서 눌리고 왼쪽 절반의 가운데에서는 아니다.
        stage.Frame({MouseAt(150.0f, 50.0f), HeldButton(MouseButton::Left)});
        Check(button->pressed, "the world button is pressed through the camera that shows it");
        stage.Frame({Released(MouseButton::Left)});
        stage.Frame({MouseAt(50.0f, 50.0f), HeldButton(MouseButton::Left)});
        Check(false == button->pressed, "and not through the other camera");
        stage.Frame({Released(MouseButton::Left)});
        // 월드 → 픽셀은 주 카메라다.
        Vector2 pixel;
        Check(screen.LayerToScreen({-10.0f, 0.0f}, world->GetScriptHandle(), pixel) && CloseTo(pixel.x, 50.0f) && CloseTo(pixel.y, 50.0f),
            "layer to screen goes through the primary camera's viewport");
        // 겹치면 depth 가 큰 카메라가 받는다. 빈 뷰포트의 카메라는 없는 것이다.
        left->viewportSize = {1.0f, 1.0f};
        left->depth = 5;
        stage.Frame({});
        Check(screen.ScreenToLayer({150.0f, 50.0f}, world->GetScriptHandle(), point) && CloseTo(point.x, 40.0f),
            "a deeper full-screen camera takes the right half from the shallower one");
        left->viewportSize = {0.0f, 1.0f};
        stage.Frame({});
        Check(screen.ScreenToLayer({150.0f, 50.0f}, world->GetScriptHandle(), point) && CloseTo(point.x, 10.0f), "an empty viewport is skipped");
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
    TestSelectionClipboardAndComposition();
    TestScriptMadeWidgetsAndCaretApi();
    TestToggleGroupsDropdownsAndTransitions();
    TestScrollbarsFollowTheContent();
    TestTooltipsModalsTabsAndDragging();
    TestSafeAreaGroupsKeyboardAndLinks();
    TestSplitCamerasMapThePointer();
    TestDropdownsMoveByKeyboardAndGamepad();
    Log::SetEchoToConsole(echo);
    std::cout << "UI scenario tests passed.\n";
    return 0;
}
