#include <JBro/InputTypes/Service/InputService.h>

#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

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

    const GamepadState& InputService::Gamepad(UInt32 slot) const
    {
        return GetView().Gamepad(slot);
    }

    void InputService::SetGamepadVibration(UInt32 slot, Float low, Float high, Float seconds) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->SetGamepadVibration(slot, low, high, seconds);
        }
    }

    void InputService::StopGamepadVibration(UInt32 slot) const
    {
        SetGamepadVibration(slot, 0.0f, 0.0f, 0.0f);
    }

    const TouchState& InputService::Touch() const
    {
        return GetView().Touch();
    }

    void InputService::InjectTouch(UInt32 id, Float x, Float y, TouchPhase phase) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->InjectTouch(id, x, y, phase);
        }
    }

    Bool InputService::EnableActionSet(NameId set) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionSetEnabled(set, true);
    }

    Bool InputService::DisableActionSet(NameId set) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionSetEnabled(set, false);
    }

    Bool InputService::IsActionSetEnabled(NameId set) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->IsActionSetEnabled(set);
    }

    UInt32 InputService::GetActionBindingCount(InputActionId action) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr ? input->GetActionBindingCount(action) : UInt32(0);
    }

    Bool InputService::GetActionBinding(InputActionId action, UInt32 index, InputBinding& out) const
    {
        const System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->GetActionBinding(action, index, out);
    }

    Bool InputService::SetActionBinding(InputActionId action, UInt32 index, const InputBinding& binding) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->SetActionBinding(action, index, binding);
    }

    Bool InputService::RemoveActionBinding(InputActionId action, UInt32 index) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->RemoveActionBinding(action, index);
    }

    Bool InputService::ResetActionBindings(InputActionId action) const
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

    Bool InputService::WriteBindingOverrides(String& out) const
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
        // 두 번의 부름 사이에 표가 바뀌지 않으므로(메인 스레드) 크기는 처음에 물은 그대로다.
        return true;
    }

    Bool InputService::ReadBindingOverrides(const String& text) const
    {
        System::IInputSystem* input = GetInputSystems().Input;
        return input != nullptr && input->ReadBindingOverrides(text.data(), text.size());
    }

    void InputService::SetGamepadDeadzones(Float stick, Float trigger) const
    {
        if (System::IInputSystem* input = GetInputSystems().Input)
        {
            input->SetGamepadDeadzones(stick, trigger);
        }
    }
}
