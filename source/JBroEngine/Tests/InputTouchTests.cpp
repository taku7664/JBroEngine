#include <JBro/Input/InputSystem.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

// 터치(D-210, input-plan §4 의 7). 기존 엔진의 규칙(뗀 프레임에도 한 번 나온다, 포커스를 잃으면 취소)을 잇는다.
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

    template<std::uint32_t Count>
    JArrayView<InputEvent> View(const InputEvent (&events)[Count])
    {
        return {events, Count};
    }

    InputEvent TouchEvent(InputEventKind kind, std::uint32_t id, float x, float y)
    {
        InputEvent event;
        event.kind = kind;
        event.codePoint = id;
        event.x = x;
        event.y = y;
        return event;
    }

    // 손가락 하나의 한 살이다. 뗀 프레임에도 한 번 나오고 다음 프레임에는 없다(기존 엔진 `e2274d1b`).
    void TestATouchLivesThroughItsPhases()
    {
        System::InputSystem input;
        InputSurfaceMapping mapping;
        mapping.originX = 10.0f;
        mapping.scaleX = 2.0f;
        const InputEvent began[] = { TouchEvent(InputEventKind::TouchBegan, 7, 20.0f, 30.0f) };
        input.BeginFrame(View(began), mapping);
        const TouchState& touch = input.GetFrame().touch;
        Check(touch.count == 1 && touch.Get(0).id == 7, "a finger that touches down is there");
        Check(touch.Get(0).phase == TouchPhase::Began && touch.Get(0).IsActive(), "and began this frame");
        Check(touch.Get(0).x == 20.0f && touch.Get(0).y == 30.0f, "at its place in game surface pixels");

        input.BeginFrame({}, mapping);
        Check(touch.count == 1 && touch.Get(0).phase == TouchPhase::Stationary, "a finger that does not move is stationary");

        const InputEvent moved[] = { TouchEvent(InputEventKind::TouchMoved, 7, 30.0f, 40.0f) };
        input.BeginFrame(View(moved), mapping);
        Check(touch.Get(0).phase == TouchPhase::Moved && touch.Get(0).x == 40.0f, "a moving finger moves");

        const InputEvent ended[] = { TouchEvent(InputEventKind::TouchEnded, 7, 35.0f, 40.0f) };
        input.BeginFrame(View(ended), mapping);
        Check(touch.count == 1 && touch.Get(0).phase == TouchPhase::Ended, "the frame a finger lifts still shows it");
        Check(false == touch.Get(0).IsActive() && touch.Get(0).x == 50.0f, "where it lifted, no longer touching");

        input.BeginFrame({}, mapping);
        Check(touch.count == 0, "and the next frame it is gone");
    }

    // 한 프레임 안에 닿고 움직이면 닿음이 먼저다. 모르는 손가락의 이동·뗌은 손가락을 만들지 않는다.
    void TestTouchesInOneFrameAndStrangers()
    {
        System::InputSystem input;
        const InputEvent quick[] =
        {
            TouchEvent(InputEventKind::TouchBegan, 1, 5.0f, 5.0f),
            TouchEvent(InputEventKind::TouchMoved, 1, 6.0f, 5.0f),
            TouchEvent(InputEventKind::TouchMoved, 99, 1.0f, 1.0f),
            TouchEvent(InputEventKind::TouchEnded, 98, 1.0f, 1.0f),
        };
        input.BeginFrame(View(quick));
        const TouchState& touch = input.GetFrame().touch;
        Check(touch.count == 1, "a finger that was never seen touching down is not made up");
        Check(touch.Get(0).phase == TouchPhase::Began && touch.Get(0).x == 6.0f, "touch down and a move in one frame is a touch down at the new place");

        // 떼기의 자리를 모르면(NaN) 마지막 자리를 남긴다.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const InputEvent lifted[] = { TouchEvent(InputEventKind::TouchEnded, 1, nan, nan) };
        input.BeginFrame(View(lifted));
        Check(touch.Get(0).phase == TouchPhase::Ended && touch.Get(0).x == 6.0f, "a lift without a place keeps the last place");
    }

    void TestTenFingersAndTheFocus()
    {
        System::InputSystem input;
        InputEvent many[MaxTouches + 2];
        for (std::uint32_t index = 0; index < MaxTouches + 2; ++index)
        {
            many[index] = TouchEvent(InputEventKind::TouchBegan, 100 + index, 1.0f, 1.0f);
        }
        input.BeginFrame(View(many));
        Check(input.GetFrame().touch.count == MaxTouches, "fingers past ten are dropped");

        InputEvent lost;
        lost.kind = InputEventKind::FocusLost;
        input.BeginFrame({&lost, 1});
        const TouchState& touch = input.GetFrame().touch;
        Check(touch.count == MaxTouches && touch.Get(0).phase == TouchPhase::Cancelled,
            "losing the focus cancels every finger - cancelled, not a tap");
        input.BeginFrame({});
        Check(input.GetFrame().touch.count == 0, "and they are gone the next frame");
    }

    // 스크립트가 만든 손가락(가상 조이스틱)은 다음 프레임에 플랫폼의 것과 같은 길로 보인다. 소비도 걸린다.
    void TestInjectedTouchesAndConsumption()
    {
        System::InputSystem input;
        BindInputSystemContext(input.GetSystemContext());
        BindInputServiceContext(input.GetServiceContext());
        const Service::InputService& service = GetInputServices().Input;
        service.InjectTouch(3, 50.0f, 60.0f, TouchPhase::Began);
        service.InjectTouch(4, 1.0f, 1.0f, TouchPhase::Stationary);
        Check(service.Touch().count == 0, "an injected finger is not there before the next frame");
        input.BeginFrame({});
        Check(service.Touch().count == 1 && service.Touch().Get(0).x == 50.0f, "the next frame shows it, unmapped");
        service.InjectTouch(3, 0.0f, 0.0f, TouchPhase::Ended);
        input.BeginFrame({});
        Check(service.Touch().Get(0).phase == TouchPhase::Ended, "and an injected lift ends it");
        input.BeginFrame({});
        Check(service.Touch().count == 0, "an injected finger is folded once, not every frame after");

        struct TouchTaker final : IInputHandler
        {
            InputResult OnInput(InputView& input) override
            {
                input.Consume(InputDevice::Touch);
                return InputResult::Pass;
            }
        } taker;
        const InputEvent down[] = { TouchEvent(InputEventKind::TouchBegan, 8, 1.0f, 2.0f) };
        input.BeginFrame(View(down));
        input.BeginDispatch();
        input.Deliver(taker);
        input.EndDispatch();
        Check(service.Touch().count == 0, "a consumed touch screen is empty below");
        BindInputSystemContext({});
        BindInputServiceContext({});
    }

    // 실제 창이다. 가짜 포인터 번호는 Windows 가 정보를 주지 않는다 - 누름은 버리고, 떼기는 자리 없이 알린다.
    void TestWindowsPointerMessages()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        WindowDesc desc;
        const char title[] = "JBro touch probe";
        desc.title = {title, static_cast<std::uint32_t>(std::strlen(title))};
        desc.width = 320;
        desc.height = 240;
        desc.visible = false;
        const WindowHandle window = platform.OpenPlatformWindow(desc);
        Check(window.value != 0, "the probe window must open");
        const HWND native = reinterpret_cast<HWND>(window.value);
        platform.ClearInputEvents();
        // **포인터 메시지는 큐에 넣을 수 없다** - `PostMessageW` 가 1002(ERROR_INVALID_MESSAGE)로 거절한다. 포인터 메시지에는
        // `TranslateMessage` 가 끼지 않으므로 창 프로시저를 곧바로 부르는 것이 실제로 도는 처리 그대로다.
        SendMessageW(native, WM_POINTERDOWN, MAKEWPARAM(4242, 0), 0);
        SendMessageW(native, WM_POINTERUP, MAKEWPARAM(4242, 0), 0);
        const JArrayView<InputEvent> events = platform.GetInputEvents();
        bool sawDown = false;
        bool sawUp = false;
        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            sawDown = sawDown || events.data[index].kind == InputEventKind::TouchBegan;
            if (events.data[index].kind == InputEventKind::TouchEnded)
            {
                sawUp = events.data[index].codePoint == 4242 && std::isnan(events.data[index].x);
            }
        }
        Check(false == sawDown, "a touch down Windows knows nothing about is dropped");
        Check(sawUp, "a lift is reported even without its place, so no finger stays down forever");
        platform.ClearInputEvents();
        platform.ClosePlatformWindow(window);
        platform.Shutdown();
    }

}

int RunInputTouchTests()
{
    TestATouchLivesThroughItsPhases();
    TestTouchesInOneFrameAndStrangers();
    TestTenFingersAndTheFocus();
    TestInjectedTouchesAndConsumption();
    TestWindowsPointerMessages();
    std::cout << "Input touch tests passed.\n";
    return 0;
}
