#include <JBro/InputTypes/InputRebinding.h>

namespace JBro
{
    InputCaptureResult CaptureBinding(const InputView& view, InputBinding& out)
    {
        out = InputBinding{};
        const KeyboardState& keyboard = view.Keyboard();
        // 물러나기가 먼저다. Escape 를 누른 프레임에 다른 키도 눌렸으면 그 키를 잡지 않는다.
        if (keyboard.keys[static_cast<std::size_t>(Key::Escape)].pressCount > 0)
        {
            return InputCaptureResult::Cancelled;
        }
        for (std::size_t index = 1; index < KeyCount; ++index)
        {
            if (keyboard.keys[index].pressCount > 0)
            {
                out.source = InputBindingSource::Key;
                out.code = static_cast<std::uint16_t>(index);
                return InputCaptureResult::Captured;
            }
        }
        const MouseState& mouse = view.Mouse();
        for (std::size_t index = 0; index < MouseButtonCount; ++index)
        {
            if (mouse.buttons[index].pressCount > 0)
            {
                out.source = InputBindingSource::MouseButton;
                out.code = static_cast<std::uint16_t>(index);
                return InputCaptureResult::Captured;
            }
        }
        for (std::uint32_t pad = 0; pad < MaxGamepads; ++pad)
        {
            const GamepadState& gamepad = view.Gamepad(pad);
            for (std::size_t index = 0; index < GamepadButtonCount; ++index)
            {
                if (gamepad.buttons[index].pressCount > 0)
                {
                    out.source = InputBindingSource::GamepadButton;
                    out.code = static_cast<std::uint16_t>(index);
                    return InputCaptureResult::Captured;
                }
            }
        }
        return InputCaptureResult::None;
    }

    bool IsSameBinding(const InputBinding& a, const InputBinding& b)
    {
        return a.source == b.source && a.code == b.code && a.composite == b.composite && a.gamepad == b.gamepad;
    }
}
