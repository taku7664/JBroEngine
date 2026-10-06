#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <cstddef>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

// XInput 은 Windows 에 기본으로 깔린 `xinput9_1_0.dll` 을 쓴다(기존 엔진과 같다). 재배포할 것이 없다.
#pragma comment(lib, "Xinput9_1_0.lib")

namespace JBro
{
    namespace
    {
        // `GamepadButton` 차례의 XInput 비트다.
        constexpr WORD ButtonFlags[static_cast<std::size_t>(GamepadButton::Count)] =
        {
            XINPUT_GAMEPAD_A,
            XINPUT_GAMEPAD_B,
            XINPUT_GAMEPAD_X,
            XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_LEFT_SHOULDER,
            XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_START,
            XINPUT_GAMEPAD_BACK,
            XINPUT_GAMEPAD_DPAD_UP,
            XINPUT_GAMEPAD_DPAD_DOWN,
            XINPUT_GAMEPAD_DPAD_LEFT,
            XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB,
        };

        Float Stick(SHORT value)
        {
            // -32768 도 -1 로 자른다. 32767 로 나누면 -1.00003 이 된다.
            return JBro::Clamp(static_cast<JBro::Float>(value) / 32767.0f, -1.0f, 1.0f);
        }
    }

    Bool WindowsPlatform::PollGamepad(UInt32 slot, GamepadRawState& state)
    {
        state = {};
        if (slot >= XUSER_MAX_COUNT)
        {
            return false;
        }
        XINPUT_STATE raw = {};
        if (XInputGetState(static_cast<DWORD>(slot), &raw) != ERROR_SUCCESS)
        {
            return false;
        }
        state.connected = true;
        for (std::size_t index = 0; index < static_cast<std::size_t>(GamepadButton::Count); ++index)
        {
            if ((raw.Gamepad.wButtons & ButtonFlags[index]) != 0)
            {
                state.buttons = static_cast<std::uint16_t>(state.buttons | (1u << index));
            }
        }
        state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = Stick(raw.Gamepad.sThumbLX);
        state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = Stick(raw.Gamepad.sThumbLY);
        state.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = Stick(raw.Gamepad.sThumbRX);
        state.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = Stick(raw.Gamepad.sThumbRY);
        state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = static_cast<JBro::Float>(raw.Gamepad.bLeftTrigger) / 255.0f;
        state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = static_cast<JBro::Float>(raw.Gamepad.bRightTrigger) / 255.0f;
        return true;
    }

    void WindowsPlatform::SetGamepadVibration(UInt32 slot, Float low, Float high)
    {
        if (slot >= XUSER_MAX_COUNT)
        {
            return;
        }
        XINPUT_VIBRATION vibration = {};
        vibration.wLeftMotorSpeed = static_cast<WORD>(JBro::Clamp(low, 0.0f, 1.0f) * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(JBro::Clamp(high, 0.0f, 1.0f) * 65535.0f);
        // 없는 자리면 실패를 돌려준다. 그것으로 할 일은 없다.
        XInputSetState(static_cast<DWORD>(slot), &vibration);
    }
}
