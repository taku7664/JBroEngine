#pragma once

#include <JBro/InputTypes/InputAction.h>
#include <JBro/InputTypes/InputState.h>

#include <cstdint>

namespace JBro
{
    namespace System
    {
        class InputSystem;
    }

    // 소비를 가르는 장치 단위다(D-210).
    enum class InputDevice : std::uint8_t
    {
        Keyboard,
        Mouse,
        // 네 자리 전부다. 자리 하나만 가져가는 소비는 두지 않는다.
        Gamepad,

        Count
    };

    // 핸들러와 폴링이 읽는 입력이다(D-210).
    //
    // 레이어 체인을 따라 **하나가 내려간다.** 위의 핸들러가 `Consume` 한 장치는 아래에서 빈 장치로
    // 보이고, 체인이 다 돈 뒤 남은 것을 `Service::InputService` 가 `OnUpdate` 의 폴링에 준다.
    //
    // 복사할 수 없다. 받은 것을 멤버로 들고 다음 프레임에 읽으면 그때의 소비 상태가 아니다 -
    // 그 프레임 안에서 다 읽는다. 만들고 되돌리는 것은 `System::InputSystem` 뿐이다. 스크립트가
    // 만들 수 있으면 위에서 막은 것을 아래에서 되살릴 수 있다.
    class InputView
    {
    public:
        InputView(const InputView&) = delete;
        InputView& operator=(const InputView&) = delete;

        // 아무 장치도 없는 뷰다. 입력 시스템이 묶이지 않은 자리(시스템 없는 테스트, 내려가는 중)가 이것을 준다.
        static const InputView& Empty()
        {
            static const InputView empty;
            return empty;
        }

        const KeyboardState& Keyboard() const
        {
            if (m_frame == nullptr || IsConsumed(InputDevice::Keyboard))
            {
                return EmptyKeyboardState;
            }
            return m_frame->keyboard;
        }

        const MouseState& Mouse() const
        {
            if (m_frame == nullptr || IsConsumed(InputDevice::Mouse))
            {
                return EmptyMouseState;
            }
            return m_frame->mouse;
        }

        // 게임패드 자리(0..3)다. 범위 밖이면 빈 패드다.
        const GamepadState& Gamepad(std::uint32_t index) const
        {
            if (m_frame == nullptr || index >= MaxGamepads || IsConsumed(InputDevice::Gamepad))
            {
                return EmptyGamepadState;
            }
            return m_frame->gamepads[index];
        }

        // 이름 붙인 입력이다(D-210). 소비된 장치의 바인딩은 빠진다 - 위에서 마우스를 가져갔으면 마우스로 묶은 액션도 아래에서는 0 이다.
        // 프로젝트에 없는 이름이면 0 이고 한 번 경고가 남는다.
        InputActionValue Action(InputActionId action) const;
        bool IsActionDown(InputActionId action) const;
        bool IsActionPressed(InputActionId action) const;
        bool IsActionReleased(InputActionId action) const;
        float GetActionFloat(InputActionId action) const;
        InputVector2 GetActionVector(InputActionId action) const;

        // 이 장치를 아래 핸들러와 폴링에게 빈 장치로 보이게 한다. 이 핸들러 자신은 계속 읽을 수 있다 -
        // 소비는 아래로 내려가는 것이지 지금 읽는 것을 지우는 일이 아니다.
        void Consume(InputDevice device)
        {
            const std::uint32_t bit = DeviceBit(device);
            m_pendingConsumed |= bit;
        }

        void ConsumeAll()
        {
            m_pendingConsumed |= AllDevices;
        }

        // 위의 핸들러가 이미 가져간 장치인가.
        bool IsConsumed(InputDevice device) const
        {
            return (m_consumed & DeviceBit(device)) != 0;
        }

    private:
        friend class System::InputSystem;

        static constexpr std::uint32_t AllDevices =
            (1u << static_cast<std::uint32_t>(InputDevice::Count)) - 1u;

        InputView() = default;

        static constexpr std::uint32_t DeviceBit(InputDevice device)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(device);
            if (index >= static_cast<std::uint32_t>(InputDevice::Count))
            {
                return 0;
            }
            return 1u << index;
        }

        const InputFrame* m_frame = nullptr;
        // 호스트의 액션 표다. 없으면 모든 액션이 0 이다.
        InputActionMap* m_actions = nullptr;
        // 위에서 가져간 것. 이 핸들러가 읽을 때 빈 장치로 보인다.
        std::uint32_t m_consumed = 0;
        // 이 핸들러가 이번에 가져간 것. 핸들러가 돌아온 뒤 `m_consumed` 에 합쳐진다.
        std::uint32_t m_pendingConsumed = 0;
    };
}
