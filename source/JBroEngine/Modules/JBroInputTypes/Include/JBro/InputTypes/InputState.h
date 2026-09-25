#pragma once

#include <JBro/Core/InputKeys.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 한 프레임의 입력 상태다(D-210). `System::InputSystem` 이 플랫폼 이벤트(D-62)를 프레임마다
    // 한 번 접어 만들고, 스크립트는 `InputView` 를 거쳐 읽기만 한다.
    //
    // 전부 고정 크기 POD 다. 게임 DLL 경계를 넘고, 프레임 경로에서 할당하지 않는다(§9).

    inline constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
    inline constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Count);
    // 한 프레임에 싣는 글자 수다. 넘치면 버린다 - 붙여넣기나 IME 가 한꺼번에 쏟아 내는 것을 막는다.
    inline constexpr std::uint32_t MaxTextPerFrame = 32;

    // 키 하나·버튼 하나의 상태다.
    //
    // **지금 눌림과 이번 프레임의 눌림 수·뗌 수를 따로 든다.** 지난 프레임과 이번 프레임의
    // 눌림을 비교하는 방식(기존 엔진의 prev/current)으로는 한 프레임 안에 눌렀다 뗀 키가 보이지
    // 않는다 - 두 시점 모두 "안 눌림" 이기 때문이다. 수는 255 에서 멈춘다.
    struct ButtonState
    {
        bool down = false;
        std::uint8_t pressCount = 0;
        std::uint8_t releaseCount = 0;
    };

    struct KeyboardState
    {
        ButtonState keys[KeyCount] = {};
        // 이번 프레임의 마지막 키 이벤트가 알려 준 조합 키다.
        KeyModifiers modifiers = KeyModifierNone;
        // 이번 프레임에 들어온 글자(유니코드 코드포인트)를 들어온 순서대로 둔다. 키와 별개다(D-62).
        std::uint32_t textLength = 0;
        std::uint32_t text[MaxTextPerFrame] = {};

        bool IsDown(Key key) const
        {
            const std::size_t index = static_cast<std::size_t>(key);
            if (index >= KeyCount)
            {
                return false;
            }
            return keys[index].down;
        }

        // 이번 프레임에 한 번이라도 눌렀다. 자동 반복은 누름이 아니다.
        bool IsPressed(Key key) const
        {
            const std::size_t index = static_cast<std::size_t>(key);
            if (index >= KeyCount)
            {
                return false;
            }
            return keys[index].pressCount > 0;
        }

        // 이번 프레임에 한 번이라도 뗐다. 창이 포커스를 잃은 프레임에는 눌려 있던 것이 모두 뗀 것이다.
        bool IsReleased(Key key) const
        {
            const std::size_t index = static_cast<std::size_t>(key);
            if (index >= KeyCount)
            {
                return false;
            }
            return keys[index].releaseCount > 0;
        }

        std::uint32_t GetTextLength() const
        {
            return textLength;
        }

        // 범위를 벗어나면 0 이다.
        std::uint32_t GetText(std::uint32_t index) const
        {
            if (index >= textLength || index >= MaxTextPerFrame)
            {
                return 0;
            }
            return text[index];
        }
    };

    struct MouseState
    {
        ButtonState buttons[MouseButtonCount] = {};
        // 위치를 한 번이라도 받았는가. 받기 전의 (0, 0) 은 위치가 아니다.
        // 막힌 장치(`InputView::Consume`)도 거짓이다 - 아래 핸들러가 구석을 가리킨다고 믿으면 안 된다.
        bool hasPosition = false;
        // 게임 화면 픽셀이다. 에디터의 게임 뷰처럼 창 안의 일부에 그려질 때는 그 사각형을 벗겨 낸 값이다.
        float x = 0.0f;
        float y = 0.0f;
        // 이번 프레임의 이동 합이다.
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        // 이번 프레임의 휠 칸 수 합이다. 세로는 위로 굴리면 양수다.
        float wheelX = 0.0f;
        float wheelY = 0.0f;

        bool IsDown(MouseButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= MouseButtonCount)
            {
                return false;
            }
            return buttons[index].down;
        }

        bool IsPressed(MouseButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= MouseButtonCount)
            {
                return false;
            }
            return buttons[index].pressCount > 0;
        }

        bool IsReleased(MouseButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= MouseButtonCount)
            {
                return false;
            }
            return buttons[index].releaseCount > 0;
        }
    };

    inline constexpr std::size_t GamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
    inline constexpr std::size_t GamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);
    // 게임패드 자리 수다. XInput 의 네 자리와 같고, 자리 번호가 곧 플레이어 번호다.
    inline constexpr std::size_t MaxGamepads = 4;

    struct GamepadState
    {
        bool connected = false;
        ButtonState buttons[GamepadButtonCount] = {};
        // 데드존을 지난 값이다. 스틱은 -1..1(위가 +), 트리거는 0..1.
        float axes[GamepadAxisCount] = {};

        bool IsDown(GamepadButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= GamepadButtonCount)
            {
                return false;
            }
            return buttons[index].down;
        }

        bool IsPressed(GamepadButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= GamepadButtonCount)
            {
                return false;
            }
            return buttons[index].pressCount > 0;
        }

        bool IsReleased(GamepadButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            if (index >= GamepadButtonCount)
            {
                return false;
            }
            return buttons[index].releaseCount > 0;
        }

        float GetAxis(GamepadAxis axis) const
        {
            const std::size_t index = static_cast<std::size_t>(axis);
            if (index >= GamepadAxisCount)
            {
                return 0.0f;
            }
            return axes[index];
        }
    };

    struct InputFrame
    {
        KeyboardState keyboard;
        MouseState mouse;
        GamepadState gamepads[MaxGamepads];
    };

    // 막힌 장치를 읽으면 이것이 나온다. 읽는 쪽이 갈래 없이 같은 멤버를 부를 수 있게 한다.
    inline constexpr KeyboardState EmptyKeyboardState{};
    inline constexpr MouseState EmptyMouseState{};
    inline constexpr GamepadState EmptyGamepadState{};

    static_assert(std::is_trivially_copyable_v<InputFrame>, "InputFrame crosses the game DLL boundary");
    static_assert(std::is_standard_layout_v<InputFrame>, "InputFrame crosses the game DLL boundary");
}
