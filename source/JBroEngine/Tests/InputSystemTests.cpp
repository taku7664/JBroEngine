#include <JBro/Input/InputSystem.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>
#include <crtdbg.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 게임 입력이 플랫폼 이벤트를 프레임 상태로 접는 것을 잰다(D-201, input-plan §4 의 1).
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

    InputEvent KeyEvent(InputEventKind kind, Key key, bool repeat = false)
    {
        InputEvent event;
        event.kind = kind;
        event.key = key;
        event.repeat = repeat;
        return event;
    }

    InputEvent ButtonEvent(InputEventKind kind, MouseButton button)
    {
        InputEvent event;
        event.kind = kind;
        event.button = button;
        return event;
    }

    InputEvent MoveEvent(float x, float y)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseMove;
        event.x = x;
        event.y = y;
        return event;
    }

    InputEvent TextEvent(std::uint32_t codePoint)
    {
        InputEvent event;
        event.kind = InputEventKind::Text;
        event.codePoint = codePoint;
        return event;
    }

    InputEvent FocusEvent(InputEventKind kind)
    {
        InputEvent event;
        event.kind = kind;
        return event;
    }

    template<std::uint32_t Count>
    JArrayView<InputEvent> View(const InputEvent (&events)[Count])
    {
        return {events, Count};
    }

    // 기존 엔진이 잃던 것이다. 한 프레임 안에 눌렀다 떼면 프레임 끝의 상태는 "안 눌림" 이지만
    // 그 프레임에 누른 것도 뗀 것도 사실이다. 가볍게 친 점프가 여기서 빠지면 안 된다.
    void TestATapInsideOneFrameIsBothPressedAndReleased()
    {
        System::InputSystem input;
        const InputEvent events[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::Space),
            KeyEvent(InputEventKind::KeyUp, Key::Space),
        };
        input.BeginFrame(View(events));
        const KeyboardState& keyboard = input.GetFrame().keyboard;
        Check(keyboard.IsPressed(Key::Space), "a tap inside one frame is a press");
        Check(keyboard.IsReleased(Key::Space), "and it is also a release");
        Check(false == keyboard.IsDown(Key::Space), "but the key is not held at the end of the frame");
        Check(false == keyboard.IsPressed(Key::A), "other keys are untouched");

        // 다음 프레임에는 남지 않는다.
        input.BeginFrame({});
        Check(false == input.GetFrame().keyboard.IsPressed(Key::Space), "a press lasts one frame");
        Check(false == input.GetFrame().keyboard.IsReleased(Key::Space), "and so does a release");
    }

    void TestAHeldKeyStaysDownWithoutNewEvents()
    {
        System::InputSystem input;
        const InputEvent down[] = { KeyEvent(InputEventKind::KeyDown, Key::W) };
        input.BeginFrame(View(down));
        Check(input.GetFrame().keyboard.IsDown(Key::W), "a pressed key is down");
        Check(input.GetFrame().keyboard.IsPressed(Key::W), "and pressed on its first frame");

        input.BeginFrame({});
        Check(input.GetFrame().keyboard.IsDown(Key::W), "it stays down while nothing new arrives");
        Check(false == input.GetFrame().keyboard.IsPressed(Key::W), "but it is not pressed again");

        const InputEvent up[] = { KeyEvent(InputEventKind::KeyUp, Key::W) };
        input.BeginFrame(View(up));
        Check(false == input.GetFrame().keyboard.IsDown(Key::W), "releasing lifts it");
        Check(input.GetFrame().keyboard.IsReleased(Key::W), "and the release is seen once");
    }

    // 자동 반복은 누름이 아니다. 누르고 있는 동안 점프가 거듭 나가면 안 된다.
    void TestARepeatIsNotAPress()
    {
        System::InputSystem input;
        const InputEvent first[] = { KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(first));

        const InputEvent repeats[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::Space, true),
            KeyEvent(InputEventKind::KeyDown, Key::Space, true),
        };
        input.BeginFrame(View(repeats));
        Check(input.GetFrame().keyboard.IsDown(Key::Space), "a repeating key is still down");
        Check(false == input.GetFrame().keyboard.IsPressed(Key::Space), "a repeat is not a press");

        // 키를 누른 채 창으로 돌아오면 첫 누름은 다른 창이 받았고 여기에는 반복만 온다.
        // 누르고 있는 것은 사실이지만 이 프레임에 누른 것은 아니다.
        System::InputSystem returning;
        const InputEvent onlyRepeats[] = { KeyEvent(InputEventKind::KeyDown, Key::W, true) };
        returning.BeginFrame(View(onlyRepeats));
        Check(returning.GetFrame().keyboard.IsDown(Key::W),
            "a repeat without a seen press still means the key is held");
        Check(false == returning.GetFrame().keyboard.IsPressed(Key::W),
            "but it is not a press on this frame");

        // 뗌을 잃고 다시 온 첫 누름도 두 번 세지 않는다.
        const InputEvent again[] = { KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(again));
        Check(false == input.GetFrame().keyboard.IsPressed(Key::Space),
            "a second down without an up is not a new press");
    }

    // 창이 포커스를 잃으면 뗌 이벤트가 오지 않는다. 조용히 지우면 "떼면 멈춘다" 가 영영 오지 않는다.
    void TestLosingFocusReleasesEverything()
    {
        System::InputSystem input;
        const InputEvent held[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::D),
            ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Left),
            MoveEvent(10.0f, 20.0f),
        };
        input.BeginFrame(View(held));
        Check(input.GetFrame().mouse.hasPosition, "the mouse has a position once it moved");

        const InputEvent lost[] = { FocusEvent(InputEventKind::FocusLost) };
        input.BeginFrame(View(lost));
        const InputFrame& frame = input.GetFrame();
        Check(false == frame.keyboard.IsDown(Key::D), "a held key is lifted when focus is lost");
        Check(frame.keyboard.IsReleased(Key::D), "and it is seen as released that frame");
        Check(false == frame.mouse.IsDown(MouseButton::Left), "a held button is lifted too");
        Check(frame.mouse.IsReleased(MouseButton::Left), "and seen as released");
        Check(false == frame.mouse.hasPosition, "the cursor position outside the window is unknown");

        // 돌아와서 처음 움직인 것은 이동이 아니다(튀지 않는다).
        const InputEvent back[] = { FocusEvent(InputEventKind::FocusGained), MoveEvent(300.0f, 200.0f) };
        input.BeginFrame(View(back));
        Check(input.GetFrame().mouse.deltaX == 0.0f && input.GetFrame().mouse.deltaY == 0.0f,
            "the first position after focus returns is not a movement");
    }

    void TestTextComesInOrderAndIsCapped()
    {
        System::InputSystem input;
        InputEvent events[MaxTextPerFrame + 4];
        for (std::uint32_t index = 0; index < MaxTextPerFrame + 4; ++index)
        {
            events[index] = TextEvent(0xAC00u + index);
        }
        input.BeginFrame(View(events));
        const KeyboardState& keyboard = input.GetFrame().keyboard;
        Check(keyboard.GetTextLength() == MaxTextPerFrame, "text is capped per frame");
        Check(keyboard.GetText(0) == 0xAC00u, "the first letter comes first");
        Check(keyboard.GetText(MaxTextPerFrame - 1) == 0xAC00u + MaxTextPerFrame - 1, "and order is kept");
        Check(keyboard.GetText(MaxTextPerFrame) == 0, "reading past the end gives nothing");

        input.BeginFrame({});
        Check(input.GetFrame().keyboard.GetTextLength() == 0, "text lasts one frame");
    }

    // 게임 스크립트가 보는 마우스는 게임 화면 픽셀이다. 에디터의 게임 뷰처럼 창의 일부에 그려질 때는
    // 그 사각형을 벗겨야 월드를 집을 수 있다(기존 엔진 `bc0bb4df`).
    void TestTheMouseIsMappedToTheGameSurface()
    {
        System::InputSystem input;
        InputSurfaceMapping mapping;
        mapping.originX = 100.0f;
        mapping.originY = 50.0f;
        mapping.scaleX = 2.0f;
        mapping.scaleY = 0.5f;

        const InputEvent first[] = { MoveEvent(110.0f, 60.0f) };
        input.BeginFrame(View(first), mapping);
        Check(input.GetFrame().mouse.x == 20.0f && input.GetFrame().mouse.y == 5.0f,
            "the position is moved into game surface pixels");
        Check(input.GetFrame().mouse.deltaX == 0.0f, "the first position is not a movement");

        const InputEvent moves[] = { MoveEvent(112.0f, 64.0f), MoveEvent(115.0f, 70.0f) };
        input.BeginFrame(View(moves), mapping);
        Check(input.GetFrame().mouse.deltaX == 10.0f && input.GetFrame().mouse.deltaY == 5.0f,
            "movement sums every move in the frame, in game pixels");

        InputEvent wheel;
        wheel.kind = InputEventKind::MouseWheel;
        wheel.y = 1.0f;
        const InputEvent wheels[] = { wheel, wheel };
        input.BeginFrame(View(wheels), mapping);
        Check(input.GetFrame().mouse.wheelY == 2.0f, "wheel notches add up");
        Check(input.GetFrame().mouse.deltaX == 0.0f, "and movement from the last frame is gone");

        input.BeginFrame({});
        Check(input.GetFrame().mouse.wheelY == 0.0f, "wheel lasts one frame");
        Check(input.GetFrame().mouse.x == 20.0f + 10.0f, "but the position stays");
    }

    void TestOutOfRangeNamesAreIgnored()
    {
        System::InputSystem input;
        const InputEvent events[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::Unknown),
            KeyEvent(InputEventKind::KeyDown, Key::Count),
            ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Count),
        };
        input.BeginFrame(View(events));
        Check(false == input.GetFrame().keyboard.IsDown(Key::Unknown), "an unknown key is never down");
        Check(false == input.GetFrame().keyboard.IsDown(Key::Count), "a name past the table is never down");
        Check(false == input.GetFrame().mouse.IsDown(MouseButton::Count), "nor is a button past the table");
    }

    // 실제 경로다. 창에 메시지를 넣고 펌프가 만든 이벤트를 그대로 접는다(D-62 의 검증 방식).
    // 한 번의 펌프 사이에 눌렀다 뗀 키가 누름과 뗌 둘 다로 보여야 한다.
    void TestPlatformEventsFoldIntoState()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        WindowDesc desc;
        const char title[] = "JBro input system probe";
        desc.title = {title, static_cast<std::uint32_t>(std::strlen(title))};
        desc.width = 320;
        desc.height = 240;
        desc.visible = false;
        const WindowHandle window = platform.OpenPlatformWindow(desc);
        Check(window.value != 0, "the probe window must open");
        const HWND native = reinterpret_cast<HWND>(window.value);

        platform.ClearInputEvents();
        PostMessageW(native, WM_KEYDOWN, VK_SPACE, 0);
        PostMessageW(native, WM_KEYUP, VK_SPACE, static_cast<LPARAM>(0xC0000001u));
        platform.PumpEvents();

        System::InputSystem input;
        input.BeginFrame(platform.GetInputEvents());
        platform.ClearInputEvents();
        Check(input.GetFrame().keyboard.IsPressed(Key::Space), "a press between two pumps is seen");
        Check(input.GetFrame().keyboard.IsReleased(Key::Space), "and so is its release");
        Check(false == input.GetFrame().keyboard.IsDown(Key::Space), "and the key is up");

        platform.ClosePlatformWindow(window);
        platform.Shutdown();
    }

    // 스크립트가 `OnUpdate` 에서 읽는 서비스다. 체인이 돌지 않은 프레임에는 이번 프레임 전체가 보이고,
    // 호스트가 묶지 않았으면 빈 입력이다(죽은 포인터가 아니다).
    void TestTheServiceReadsThisFrame()
    {
        System::InputSystem input;
        BindInputSystemContext(input.GetSystemContext());
        BindInputServiceContext(input.GetServiceContext());

        const InputEvent events[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::W),
            ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Left),
        };
        input.BeginFrame(View(events));
        const Service::InputService& service = GetInputServices().Input;
        Check(service.Keyboard().IsDown(Key::W), "the service sees a held key");
        Check(service.Keyboard().IsPressed(Key::W), "and its press on this frame");
        Check(service.Mouse().IsDown(MouseButton::Left), "the service sees a held button");
        Check(false == service.GetView().IsConsumed(InputDevice::Keyboard),
            "nothing has taken the keyboard when no chain ran");

        BindInputSystemContext({});
        BindInputServiceContext({});
        Check(false == GetInputServices().Input.Keyboard().IsDown(Key::W),
            "an unbound service reads an empty keyboard");
        Check(false == GetInputServices().Input.Mouse().hasPosition, "and an empty mouse");
    }

    // 프레임마다 도는 자리다. 이벤트가 있어도 없어도 힙을 건드리지 않는다(§9).
    void TestFoldingDoesNotAllocate()
    {
#if defined(_MSC_VER) && defined(_DEBUG)
        System::InputSystem input;
        const InputEvent events[] =
        {
            KeyEvent(InputEventKind::KeyDown, Key::A),
            TextEvent('a'),
            MoveEvent(1.0f, 2.0f),
            ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Right),
            KeyEvent(InputEventKind::KeyUp, Key::A),
            FocusEvent(InputEventKind::FocusLost),
        };
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
        for (int frame = 0; frame < 200; ++frame)
        {
            input.BeginFrame(View(events));
            input.BeginFrame({});
        }
        _CrtSetAllocHook(previous);
        Check(g_crtAllocations.load() == 0, "folding input does not touch the CRT heap");
#endif
    }
}

int RunInputSystemTests()
{
    TestATapInsideOneFrameIsBothPressedAndReleased();
    TestAHeldKeyStaysDownWithoutNewEvents();
    TestARepeatIsNotAPress();
    TestLosingFocusReleasesEverything();
    TestTextComesInOrderAndIsCapped();
    TestTheMouseIsMappedToTheGameSurface();
    TestOutOfRangeNamesAreIgnored();
    TestPlatformEventsFoldIntoState();
    TestTheServiceReadsThisFrame();
    TestFoldingDoesNotAllocate();
    std::cout << "Input system tests passed.\n";
    return 0;
}
