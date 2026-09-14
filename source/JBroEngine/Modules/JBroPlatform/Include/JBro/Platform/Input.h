#pragma once

#include <JBro/Core/Core.h>

namespace JBro
{
    // 플랫폼이 모아 주는 입력이다(D-62).
    //
    // **상태가 아니라 이벤트다.** 매 프레임 키 배열을 읽어 가면 한 프레임 안에 눌렀다
    // 뗀 키와 글자 입력 순서가 사라진다. 에디터의 텍스트 필드가 바로 그것을 필요로 한다.
    //
    // 값은 전부 POD 다. 게임 DLL 경계를 넘어간다.

    enum class InputEventKind : std::uint8_t
    {
        KeyDown,
        KeyUp,
        // 글자 하나다. `Key` 와 별개다 - 같은 키라도 배열과 조합에 따라 다른 글자가 된다.
        Text,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        FocusGained,
        FocusLost
    };

    enum class MouseButton : std::uint8_t
    {
        Left,
        Right,
        Middle,
        Extra1,
        Extra2,
        Count
    };

    // 비트 조합이다. `KeyModifierShift | KeyModifierControl` 처럼 쓴다.
    using KeyModifiers = std::uint8_t;
    inline constexpr KeyModifiers KeyModifierNone = 0;
    inline constexpr KeyModifiers KeyModifierShift = 1 << 0;
    inline constexpr KeyModifiers KeyModifierControl = 1 << 1;
    inline constexpr KeyModifiers KeyModifierAlt = 1 << 2;
    inline constexpr KeyModifiers KeyModifierSuper = 1 << 3;

    // 물리 키다. 배열과 무관한 이름을 쓴다 - `Key::A` 는 QWERTY 의 A 자리이지
    // 그 키가 내는 글자가 아니다. 글자는 `InputEventKind::Text` 로 따로 온다.
    enum class Key : std::uint16_t
    {
        Unknown = 0,

        Tab,
        Left,
        Right,
        Up,
        Down,
        PageUp,
        PageDown,
        Home,
        End,
        Insert,
        Delete,
        Backspace,
        Space,
        Enter,
        Escape,

        LeftControl,
        LeftShift,
        LeftAlt,
        LeftSuper,
        RightControl,
        RightShift,
        RightAlt,
        RightSuper,
        Menu,

        Digit0,
        Digit1,
        Digit2,
        Digit3,
        Digit4,
        Digit5,
        Digit6,
        Digit7,
        Digit8,
        Digit9,

        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        Apostrophe,
        Comma,
        Minus,
        Period,
        Slash,
        Semicolon,
        Equal,
        LeftBracket,
        Backslash,
        RightBracket,
        GraveAccent,

        CapsLock,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,

        Keypad0,
        Keypad1,
        Keypad2,
        Keypad3,
        Keypad4,
        Keypad5,
        Keypad6,
        Keypad7,
        Keypad8,
        Keypad9,
        KeypadDecimal,
        KeypadDivide,
        KeypadMultiply,
        KeypadSubtract,
        KeypadAdd,
        KeypadEnter,

        Count
    };

    struct InputEvent
    {
        InputEventKind kind = InputEventKind::FocusLost;
        Key key = Key::Unknown;
        MouseButton button = MouseButton::Left;
        KeyModifiers modifiers = KeyModifierNone;
        // 자동 반복으로 다시 온 KeyDown 이다.
        bool repeat = false;
        // MouseMove 는 클라이언트 영역 픽셀 좌표, MouseWheel 은 칸 수다.
        // 나머지 종류에서는 0 이다.
        float x = 0.0f;
        float y = 0.0f;
        // Text 의 유니코드 코드포인트다. 나머지 종류에서는 0 이다.
        std::uint32_t codePoint = 0;
    };

    static_assert(sizeof(InputEvent) == 20, "InputEvent crosses the game DLL boundary");
}
