#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    struct Probe
    {
        JBro::WindowsPlatform platform;
        JBro::WindowHandle window;
        HWND native = nullptr;

        void Open(const char* title)
        {
            JBro::JMemoryContext memory;
            Check(platform.Initialize(memory), "the platform must initialize");
            JBro::WindowDesc desc;
            desc.title = {title, static_cast<std::uint32_t>(std::strlen(title))};
            desc.width = 320;
            desc.height = 240;
            desc.visible = false;
            window = platform.OpenPlatformWindow(desc);
            Check(window.value != 0, "the probe window must open");
            native = reinterpret_cast<HWND>(window.value);
        }

        void Close()
        {
            if (window.value != 0)
            {
                platform.ClosePlatformWindow(window);
                window = {};
            }
            platform.Shutdown();
        }

        // 메시지를 큐에 넣고 펌프를 돈다. **SendMessage 로 WndProc 을 직접 부르지 않는다** -
        // 그러면 PeekMessage 와 TranslateMessage 를 건너뛰어, 실제로 도는 경로가 아닌
        // 다른 경로를 재게 된다. WM_CHAR 는 TranslateMessage 가 만들어 주는 것이다.
        void PostAndPump(UINT message, WPARAM wParam, LPARAM lParam)
        {
            // **꺼내 간 셈 치고 비운다**(D-177). 펌프는 쌓기만 하므로, 한 번에 하나를 보려면
            // 부르는 쪽이 앞의 것을 비워야 한다 - 실제 프로그램에서는 UI 에 넣어 준 뒤 비운다.
            platform.ClearInputEvents();
            PostMessageW(native, message, wParam, lParam);
            platform.PumpEvents();
        }
    };

    const JBro::InputEvent* FindFirst(
        JBro::JArrayView<JBro::InputEvent> events, JBro::InputEventKind kind)
    {
        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            if (events.data[index].kind == kind)
            {
                return &events.data[index];
            }
        }
        return nullptr;
    }

    // **꺼내 가지 않은 입력은 다음 펌프에도 남는다**(D-177). 예전에는 펌프가 먼저 비워서,
    // 한 프레임에 펌프가 두 번 도는 에디터에서 뒤의 펌프가 앞의 입력을 지웠다 - 빠르게 친
    // 글자가 하나씩 빠졌다(실제 에디터에서 `Beta` 가 `Bea` 로 들어갔다).
    void TestUnreadInputSurvivesAnotherPump()
    {
        Probe probe;
        probe.Open("JBro input keep probe");

        probe.PostAndPump(WM_KEYDOWN, VK_LEFT, 0);
        Check(FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown) != nullptr,
            "the first press must be there");
        const std::uint32_t afterFirst = probe.platform.GetInputEvents().size;

        // 아무도 꺼내 가지 않은 채 한 번 더 돈다. 앞의 것이 그대로 있어야 한다.
        PostMessageW(probe.native, WM_KEYDOWN, VK_RIGHT, 0);
        probe.platform.PumpEvents();
        JBro::JArrayView<JBro::InputEvent> events = probe.platform.GetInputEvents();
        Check(events.size > afterFirst, "the second pump must add to what was there");
        bool sawLeft = false;
        bool sawRight = false;
        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            if (events.data[index].kind != JBro::InputEventKind::KeyDown)
            {
                continue;
            }
            sawLeft = sawLeft || events.data[index].key == JBro::Key::Left;
            sawRight = sawRight || events.data[index].key == JBro::Key::Right;
        }
        Check(sawLeft && sawRight, "both presses must still be readable");

        // 비우면 사라진다. 그것이 꺼내 간 쪽의 몫이다.
        probe.platform.ClearInputEvents();
        Check(probe.platform.GetInputEvents().size == 0, "clearing empties the queue");
        probe.Close();
    }

    void TestKeysComeOutAsKeys()
    {
        Probe probe;
        probe.Open("JBro input key probe");

        probe.PostAndPump(WM_KEYDOWN, VK_LEFT, 0);
        const JBro::InputEvent* down =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr, "a key press must produce a key down event");
        Check(down->key == JBro::Key::Left, "and it must name the key that was pressed");
        Check(false == down->repeat, "a first press is not a repeat");

        // 30번 비트가 자동 반복이다.
        probe.PostAndPump(WM_KEYDOWN, VK_LEFT, static_cast<LPARAM>(1) << 30);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->repeat, "a held key must be marked as a repeat");

        probe.PostAndPump(WM_KEYUP, VK_LEFT, 0);
        const JBro::InputEvent* up =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyUp);
        Check(up != nullptr && up->key == JBro::Key::Left, "and releasing it must be reported");

        // 글자 키는 배열과 무관한 물리 키 이름으로 온다.
        probe.PostAndPump(WM_KEYDOWN, 'A', 0);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::A, "letters must map to letter keys");

        probe.PostAndPump(WM_KEYDOWN, VK_F7, 0);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::F7, "function keys must map too");

        // **Enter 와 키패드 Enter 는 같은 가상 키다.** 확장 비트만 다르다.
        probe.PostAndPump(WM_KEYDOWN, VK_RETURN, 0);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::Enter, "return must be the return key");

        probe.PostAndPump(WM_KEYDOWN, VK_RETURN, static_cast<LPARAM>(1) << 24);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::KeypadEnter,
            "and the keypad one must be told apart by its extended bit");

        // 좌우 Control 도 같은 가상 키다.
        probe.PostAndPump(WM_KEYDOWN, VK_CONTROL, 0);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::LeftControl,
            "control without the extended bit is the left one");

        probe.PostAndPump(WM_KEYDOWN, VK_CONTROL, static_cast<LPARAM>(1) << 24);
        down = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::KeyDown);
        Check(down != nullptr && down->key == JBro::Key::RightControl,
            "and with it, the right one");

        probe.Close();
    }

    void TestTextComesOutSeparately()
    {
        Probe probe;
        probe.Open("JBro input text probe");

        // 글자는 WM_CHAR 로 온다. 키 이벤트와 별개다.
        probe.PostAndPump(WM_CHAR, static_cast<WPARAM>(L'k'), 0);
        const JBro::InputEvent* text =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::Text);
        Check(text != nullptr, "a character message must produce a text event");
        Check(text->codePoint == static_cast<std::uint32_t>('k'),
            "and carry the code point");

        // 한글도 그대로 지나가야 한다.
        probe.PostAndPump(WM_CHAR, static_cast<WPARAM>(0xAC00), 0);
        text = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::Text);
        Check(text != nullptr && text->codePoint == 0xAC00,
            "characters outside ASCII must survive");

        // **BMP 밖 글자는 UTF-16 서러게이트 쌍으로 두 번에 나눠 온다.**
        // 앞쪽만으로는 글자가 아니므로 아무것도 내보내지 않고 들고 있어야 한다.
        // 앞의 글자들을 먼저 비운다. 펌프는 쌓기만 하므로(D-177) 비우지 않으면 여기서
        // 찾는 것이 방금 친 반쪽이 아니라 앞의 글자다.
        probe.platform.ClearInputEvents();
        PostMessageW(probe.native, WM_CHAR, static_cast<WPARAM>(0xD83D), 0);
        probe.platform.PumpEvents();
        Check(FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::Text) == nullptr,
            "half of a surrogate pair is not a character yet");

        probe.PostAndPump(WM_CHAR, static_cast<WPARAM>(0xDE00), 0);
        text = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::Text);
        Check(text != nullptr && text->codePoint == 0x1F600,
            "the two halves must be joined into one code point");

        // 제어 문자는 글자가 아니다. Backspace 는 키 이벤트로 이미 갔다.
        probe.PostAndPump(WM_CHAR, static_cast<WPARAM>(8), 0);
        Check(FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::Text) == nullptr,
            "control characters must not arrive as text");

        probe.Close();
    }

    void TestTheMouseComesOut()
    {
        Probe probe;
        probe.Open("JBro input mouse probe");

        probe.PostAndPump(WM_MOUSEMOVE, 0, MAKELPARAM(37, 91));
        const JBro::InputEvent* move =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::MouseMove);
        Check(move != nullptr, "moving the mouse must be reported");
        Check(move->x == 37.0f && move->y == 91.0f, "in client pixels");

        probe.PostAndPump(WM_RBUTTONDOWN, 0, MAKELPARAM(5, 6));
        const JBro::InputEvent* press =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::MouseButtonDown);
        Check(press != nullptr && press->button == JBro::MouseButton::Right,
            "and so must the button that was pressed");
        Check(press->x == 5.0f && press->y == 6.0f, "with the position it happened at");

        probe.PostAndPump(WM_MBUTTONUP, 0, 0);
        const JBro::InputEvent* release =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::MouseButtonUp);
        Check(release != nullptr && release->button == JBro::MouseButton::Middle,
            "releasing the middle button must be told apart from the others");

        // 휠은 칸 수로 준다. 120 이 한 칸이다.
        probe.PostAndPump(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        const JBro::InputEvent* wheel =
            FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::MouseWheel);
        Check(wheel != nullptr && wheel->y == 1.0f, "one wheel notch must read as one");

        probe.PostAndPump(WM_MOUSEHWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        wheel = FindFirst(probe.platform.GetInputEvents(), JBro::InputEventKind::MouseWheel);
        Check(wheel != nullptr && wheel->x == -1.0f,
            "and the horizontal wheel must land on the other axis");

        probe.Close();
    }

    void TestThePumpOwnsTheList()
    {
        Probe probe;
        probe.Open("JBro input lifetime probe");

        Check(probe.platform.GetInputEvents().size == 0,
            "nothing has been pumped yet, so there is nothing to read");

        probe.PostAndPump(WM_KEYDOWN, VK_SPACE, 0);
        Check(probe.platform.GetInputEvents().size > 0, "the pump must gather what arrived");

        // **비우는 것은 꺼내 간 쪽의 몫이다**(D-177). 펌프가 먼저 비우면 한 프레임에 펌프가
        // 두 번 도는 자리(에디터)에서 뒤의 펌프가 앞의 입력을 아무도 못 본 채 지운다.
        //
        // 그렇다고 끝없이 자라지는 않는다 - 꺼내 간 쪽이 비우고(에디터), 꺼내 가는 쪽이 없으면
        // 엔진이 프레임마다 비운다. 여기서는 그 약속의 앞쪽을 잰다.
        probe.platform.PumpEvents();
        Check(probe.platform.GetInputEvents().size > 0,
            "the next pump must leave what nobody has read yet");
        probe.platform.ClearInputEvents();
        Check(probe.platform.GetInputEvents().size == 0,
            "and clearing is what empties it");

        // 창을 닫아도 플랫폼은 살아 있다. 남은 메시지가 사라진 창을 건드리면 안 된다.
        probe.Close();
    }
}

int RunInputTests()
{
    TestUnreadInputSurvivesAnotherPump();
    TestKeysComeOutAsKeys();
    TestTextComesOutSeparately();
    TestTheMouseComesOut();
    TestThePumpOwnsTheList();
    std::cout << "Input tests passed.\n";
    return 0;
}
