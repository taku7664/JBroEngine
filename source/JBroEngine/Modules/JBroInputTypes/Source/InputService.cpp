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
}
