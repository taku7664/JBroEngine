#pragma once

#include <cstdint>

namespace JBro
{
    // 키와 버튼의 이름이다(D-62, D-201). 차원과 무관한 공개 값 타입이라 JBroCore 에 한 번만 둔다 -
    // 플랫폼(이벤트를 만드는 쪽)과 스크립트(상태를 읽는 쪽)가 같은 이름을 써야 하는데, 스크립트는
    // Tier E 인 JBroPlatform 을 볼 수 없다. 값을 바꾸거나 끼워 넣으면 입력 상태 배열의 모양이 바뀐다.

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
}
