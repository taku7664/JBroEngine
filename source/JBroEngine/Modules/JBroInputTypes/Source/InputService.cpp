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

    std::uint32_t InputService::GetActionBindingCount(InputActionId action) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr ? input->GetActionBindingCount(action) : 0;
    }

    bool InputService::GetActionBinding(InputActionId action, std::uint32_t index, InputBinding& out) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->GetActionBinding(action, index, out);
    }

    bool InputService::SetActionBinding(InputActionId action, std::uint32_t index, const InputBinding& binding) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionBinding(action, index, binding);
    }

    bool InputService::RemoveActionBinding(InputActionId action, std::uint32_t index) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->RemoveActionBinding(action, index);
    }

    bool InputService::ResetActionBindings(InputActionId action) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->ResetActionBindings(action);
    }

    void InputService::ResetAllActionBindings() const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->ResetAllActionBindings();
        }
    }

    InputCaptureResult InputService::CaptureBinding(InputBinding& out) const
    {
        return JBro::CaptureBinding(GetView(), out);
    }

    bool InputService::WriteBindingOverrides(String& out) const
    {
        out.clear();
        const System::IInputSystem* input = GetInputSystems().Input;
        if (input == nullptr)
        {
            return false;
        }
        // 크기를 먼저 묻고 이 사본(게임 DLL)이 버퍼를 키운다. 호스트는 받은 버퍼에 쓰기만 한다.
        std::size_t size = 0;
        if (input->WriteBindingOverrides(nullptr, 0, size))
        {
            return true;
        }
        out.resize(size);
        if (false == input->WriteBindingOverrides(out.data(), out.size(), size))
        {
            out.clear();
            return false;
        }
        out.resize(size);
        return true;
    }

    bool InputService::ReadBindingOverrides(const String& text) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->ReadBindingOverrides(text.data(), text.size());
    }

    void InputService::SetGamepadDeadzones(float stick, float trigger) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->SetGamepadDeadzones(stick, trigger);
        }
    }
}
