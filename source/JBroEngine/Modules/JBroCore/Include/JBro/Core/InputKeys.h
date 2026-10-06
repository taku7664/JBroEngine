#pragma once

#include <cstdint>
#include <JBro/Types/Bool.h>

namespace JBro
{
    // 키와 버튼의 이름이다(D-62, D-214). 차원과 무관한 공개 값 타입이라 JBroCore 에 한 번만 둔다 -
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

    // 게임패드 버튼이다. 이름은 자리로 부른다(Xbox A = PlayStation X = `South`). 기존 엔진 `EGamepadButton` 과 이름이 같다 -
    // `.jproject` 의 바인딩이 이 글자로 적혀 있다.
    enum class GamepadButton : std::uint8_t
    {
        South,
        East,
        West,
        North,
        LeftShoulder,
        RightShoulder,
        Start,
        Select,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        LeftThumb,
        RightThumb,

        Count
    };

    // 게임패드 축이다. 스틱은 -1..1(위가 +), 트리거는 0..1 이다.
    enum class GamepadAxis : std::uint8_t
    {
        LeftX,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,

        Count
    };

    // 이름과 값을 잇는다(D-214). 프로젝트 파일의 입력 바인딩과 에디터의 목록이 쓴다. 매 프레임 경로에서는 부르지 않는다.
    // 이름은 열거자 이름 그대로이고, 읽을 때는 기존 엔진의 옛 이름(`Num0`·`LeftCtrl`·`Equals`·`Grave`·`Numpad0` 따위)도 받는다.
    // 모르는 값이면 이름은 빈 글자, 찾기는 거짓이다.
    const char* GetKeyName(Key key);
    Bool FindKeyByName(const char* name, Key& key);
    const char* GetMouseButtonName(MouseButton button);
    Bool FindMouseButtonByName(const char* name, MouseButton& button);
    const char* GetGamepadButtonName(GamepadButton button);
    Bool FindGamepadButtonByName(const char* name, GamepadButton& button);
    const char* GetGamepadAxisName(GamepadAxis axis);
    Bool FindGamepadAxisByName(const char* name, GamepadAxis& axis);
}
