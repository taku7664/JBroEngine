#include <JBro/InputTypes/Service/InputService.h>

#include <JBro/InputTypes/Internal/SystemContext.h>

namespace JBro::Service
{
    const InputView& InputService::GetView() const
    {
        // 호스트가 입력 시스템을 묶지 않았으면(시스템이 없는 테스트, 내려가는 중) 빈 입력이다.
        System::IInputSystem* input = GetInputSystems().Input;
        if (input == nullptr)
        {
            return InputView::Empty();
        }
        return input->GetResidualView();
    }

    const KeyboardState& InputService::Keyboard() const
    {
        return GetView().Keyboard();
    }

    const MouseState& InputService::Mouse() const
    {
        return GetView().Mouse();
    }

    const GamepadState& InputService::Gamepad(std::uint32_t slot) const
    {
        return GetView().Gamepad(slot);
    }

    void InputService::SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->SetGamepadVibration(slot, low, high, seconds);
        }
    }

    void InputService::StopGamepadVibration(std::uint32_t slot) const
    {
        SetGamepadVibration(slot, 0.0f, 0.0f, 0.0f);
    }

    const TouchState& InputService::Touch() const
    {
        return GetView().Touch();
    }

    void InputService::InjectTouch(std::uint32_t id, float x, float y, TouchPhase phase) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->InjectTouch(id, x, y, phase);
        }
    }

    bool InputService::EnableActionSet(NameId set) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionSetEnabled(set, true);
    }

    bool InputService::DisableActionSet(NameId set) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionSetEnabled(set, false);
    }

    bool InputService::IsActionSetEnabled(NameId set) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->IsActionSetEnabled(set);
    }

    void InputService::SetGamepadDeadzones(float stick, float trigger) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->SetGamepadDeadzones(stick, trigger);
        }
    }
}
