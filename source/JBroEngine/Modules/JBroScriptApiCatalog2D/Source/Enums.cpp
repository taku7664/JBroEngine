#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
void AddEnums(ApiCatalog& catalog)
{
    {
    auto& type = AddType(catalog, "SpriteFlip", "JBro::Component::SpriteFlip", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::SpriteFlip>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "SpriteSizeMode", "JBro::Component::SpriteSizeMode", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::SpriteSizeMode>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "SpritePivotMode", "JBro::Component::SpritePivotMode", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::SpritePivotMode>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "CameraProjection2D", "JBro::Component::CameraProjection2D", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::CameraProjection2D>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "BodyType2D", "JBro::Component::BodyType2D", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::BodyType2D>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "ColliderShape2D", "JBro::Component::ColliderShape2D", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::ColliderShape2D>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "Light2DType", "JBro::Component::Light2DType", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::Light2DType>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "ShadowShape2D", "JBro::Component::ShadowShape2D", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::ShadowShape2D>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "TextOverflow", "JBro::Component::TextOverflow", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::TextOverflow>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "TextWrapMode", "JBro::Component::TextWrapMode", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::TextWrapMode>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "TextAlignX", "JBro::Component::TextAlignX", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::TextAlignX>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "TextAlignY", "JBro::Component::TextAlignY", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::TextAlignY>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "AudioSourceState", "JBro::Component::AudioSourceState", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<Component::AudioSourceState>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "AudioAttenuation", "JBro::AudioAttenuation", ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<AudioAttenuation>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog, "MouseButton", "JBro::MouseButton", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(MouseButton::Left), MouseButton>);
    type.EnumValues.Add("Left");
    static_assert(std::is_same_v<decltype(MouseButton::Right), MouseButton>);
    type.EnumValues.Add("Right");
    static_assert(std::is_same_v<decltype(MouseButton::Middle), MouseButton>);
    type.EnumValues.Add("Middle");
    static_assert(std::is_same_v<decltype(MouseButton::Extra1), MouseButton>);
    type.EnumValues.Add("Extra1");
    static_assert(std::is_same_v<decltype(MouseButton::Extra2), MouseButton>);
    type.EnumValues.Add("Extra2");
    static_assert(std::is_same_v<decltype(MouseButton::Count), MouseButton>);
    type.EnumValues.Add("Count");
    }
    {
    auto& type = AddType(catalog, "Key", "JBro::Key", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Key::Unknown), Key>);
    type.EnumValues.Add("Unknown");
    static_assert(std::is_same_v<decltype(Key::Tab), Key>);
    type.EnumValues.Add("Tab");
    static_assert(std::is_same_v<decltype(Key::Left), Key>);
    type.EnumValues.Add("Left");
    static_assert(std::is_same_v<decltype(Key::Right), Key>);
    type.EnumValues.Add("Right");
    static_assert(std::is_same_v<decltype(Key::Up), Key>);
    type.EnumValues.Add("Up");
    static_assert(std::is_same_v<decltype(Key::Down), Key>);
    type.EnumValues.Add("Down");
    static_assert(std::is_same_v<decltype(Key::PageUp), Key>);
    type.EnumValues.Add("PageUp");
    static_assert(std::is_same_v<decltype(Key::PageDown), Key>);
    type.EnumValues.Add("PageDown");
    static_assert(std::is_same_v<decltype(Key::Home), Key>);
    type.EnumValues.Add("Home");
    static_assert(std::is_same_v<decltype(Key::End), Key>);
    type.EnumValues.Add("End");
    static_assert(std::is_same_v<decltype(Key::Insert), Key>);
    type.EnumValues.Add("Insert");
    static_assert(std::is_same_v<decltype(Key::Delete), Key>);
    type.EnumValues.Add("Delete");
    static_assert(std::is_same_v<decltype(Key::Backspace), Key>);
    type.EnumValues.Add("Backspace");
    static_assert(std::is_same_v<decltype(Key::Space), Key>);
    type.EnumValues.Add("Space");
    static_assert(std::is_same_v<decltype(Key::Enter), Key>);
    type.EnumValues.Add("Enter");
    static_assert(std::is_same_v<decltype(Key::Escape), Key>);
    type.EnumValues.Add("Escape");
    static_assert(std::is_same_v<decltype(Key::LeftControl), Key>);
    type.EnumValues.Add("LeftControl");
    static_assert(std::is_same_v<decltype(Key::LeftShift), Key>);
    type.EnumValues.Add("LeftShift");
    static_assert(std::is_same_v<decltype(Key::LeftAlt), Key>);
    type.EnumValues.Add("LeftAlt");
    static_assert(std::is_same_v<decltype(Key::LeftSuper), Key>);
    type.EnumValues.Add("LeftSuper");
    static_assert(std::is_same_v<decltype(Key::RightControl), Key>);
    type.EnumValues.Add("RightControl");
    static_assert(std::is_same_v<decltype(Key::RightShift), Key>);
    type.EnumValues.Add("RightShift");
    static_assert(std::is_same_v<decltype(Key::RightAlt), Key>);
    type.EnumValues.Add("RightAlt");
    static_assert(std::is_same_v<decltype(Key::RightSuper), Key>);
    type.EnumValues.Add("RightSuper");
    static_assert(std::is_same_v<decltype(Key::Menu), Key>);
    type.EnumValues.Add("Menu");
    static_assert(std::is_same_v<decltype(Key::Digit0), Key>);
    type.EnumValues.Add("Digit0");
    static_assert(std::is_same_v<decltype(Key::Digit1), Key>);
    type.EnumValues.Add("Digit1");
    static_assert(std::is_same_v<decltype(Key::Digit2), Key>);
    type.EnumValues.Add("Digit2");
    static_assert(std::is_same_v<decltype(Key::Digit3), Key>);
    type.EnumValues.Add("Digit3");
    static_assert(std::is_same_v<decltype(Key::Digit4), Key>);
    type.EnumValues.Add("Digit4");
    static_assert(std::is_same_v<decltype(Key::Digit5), Key>);
    type.EnumValues.Add("Digit5");
    static_assert(std::is_same_v<decltype(Key::Digit6), Key>);
    type.EnumValues.Add("Digit6");
    static_assert(std::is_same_v<decltype(Key::Digit7), Key>);
    type.EnumValues.Add("Digit7");
    static_assert(std::is_same_v<decltype(Key::Digit8), Key>);
    type.EnumValues.Add("Digit8");
    static_assert(std::is_same_v<decltype(Key::Digit9), Key>);
    type.EnumValues.Add("Digit9");
    static_assert(std::is_same_v<decltype(Key::A), Key>);
    type.EnumValues.Add("A");
    static_assert(std::is_same_v<decltype(Key::B), Key>);
    type.EnumValues.Add("B");
    static_assert(std::is_same_v<decltype(Key::C), Key>);
    type.EnumValues.Add("C");
    static_assert(std::is_same_v<decltype(Key::D), Key>);
    type.EnumValues.Add("D");
    static_assert(std::is_same_v<decltype(Key::E), Key>);
    type.EnumValues.Add("E");
    static_assert(std::is_same_v<decltype(Key::F), Key>);
    type.EnumValues.Add("F");
    static_assert(std::is_same_v<decltype(Key::G), Key>);
    type.EnumValues.Add("G");
    static_assert(std::is_same_v<decltype(Key::H), Key>);
    type.EnumValues.Add("H");
    static_assert(std::is_same_v<decltype(Key::I), Key>);
    type.EnumValues.Add("I");
    static_assert(std::is_same_v<decltype(Key::J), Key>);
    type.EnumValues.Add("J");
    static_assert(std::is_same_v<decltype(Key::K), Key>);
    type.EnumValues.Add("K");
    static_assert(std::is_same_v<decltype(Key::L), Key>);
    type.EnumValues.Add("L");
    static_assert(std::is_same_v<decltype(Key::M), Key>);
    type.EnumValues.Add("M");
    static_assert(std::is_same_v<decltype(Key::N), Key>);
    type.EnumValues.Add("N");
    static_assert(std::is_same_v<decltype(Key::O), Key>);
    type.EnumValues.Add("O");
    static_assert(std::is_same_v<decltype(Key::P), Key>);
    type.EnumValues.Add("P");
    static_assert(std::is_same_v<decltype(Key::Q), Key>);
    type.EnumValues.Add("Q");
    static_assert(std::is_same_v<decltype(Key::R), Key>);
    type.EnumValues.Add("R");
    static_assert(std::is_same_v<decltype(Key::S), Key>);
    type.EnumValues.Add("S");
    static_assert(std::is_same_v<decltype(Key::T), Key>);
    type.EnumValues.Add("T");
    static_assert(std::is_same_v<decltype(Key::U), Key>);
    type.EnumValues.Add("U");
    static_assert(std::is_same_v<decltype(Key::V), Key>);
    type.EnumValues.Add("V");
    static_assert(std::is_same_v<decltype(Key::W), Key>);
    type.EnumValues.Add("W");
    static_assert(std::is_same_v<decltype(Key::X), Key>);
    type.EnumValues.Add("X");
    static_assert(std::is_same_v<decltype(Key::Y), Key>);
    type.EnumValues.Add("Y");
    static_assert(std::is_same_v<decltype(Key::Z), Key>);
    type.EnumValues.Add("Z");
    static_assert(std::is_same_v<decltype(Key::F1), Key>);
    type.EnumValues.Add("F1");
    static_assert(std::is_same_v<decltype(Key::F2), Key>);
    type.EnumValues.Add("F2");
    static_assert(std::is_same_v<decltype(Key::F3), Key>);
    type.EnumValues.Add("F3");
    static_assert(std::is_same_v<decltype(Key::F4), Key>);
    type.EnumValues.Add("F4");
    static_assert(std::is_same_v<decltype(Key::F5), Key>);
    type.EnumValues.Add("F5");
    static_assert(std::is_same_v<decltype(Key::F6), Key>);
    type.EnumValues.Add("F6");
    static_assert(std::is_same_v<decltype(Key::F7), Key>);
    type.EnumValues.Add("F7");
    static_assert(std::is_same_v<decltype(Key::F8), Key>);
    type.EnumValues.Add("F8");
    static_assert(std::is_same_v<decltype(Key::F9), Key>);
    type.EnumValues.Add("F9");
    static_assert(std::is_same_v<decltype(Key::F10), Key>);
    type.EnumValues.Add("F10");
    static_assert(std::is_same_v<decltype(Key::F11), Key>);
    type.EnumValues.Add("F11");
    static_assert(std::is_same_v<decltype(Key::F12), Key>);
    type.EnumValues.Add("F12");
    static_assert(std::is_same_v<decltype(Key::Apostrophe), Key>);
    type.EnumValues.Add("Apostrophe");
    static_assert(std::is_same_v<decltype(Key::Comma), Key>);
    type.EnumValues.Add("Comma");
    static_assert(std::is_same_v<decltype(Key::Minus), Key>);
    type.EnumValues.Add("Minus");
    static_assert(std::is_same_v<decltype(Key::Period), Key>);
    type.EnumValues.Add("Period");
    static_assert(std::is_same_v<decltype(Key::Slash), Key>);
    type.EnumValues.Add("Slash");
    static_assert(std::is_same_v<decltype(Key::Semicolon), Key>);
    type.EnumValues.Add("Semicolon");
    static_assert(std::is_same_v<decltype(Key::Equal), Key>);
    type.EnumValues.Add("Equal");
    static_assert(std::is_same_v<decltype(Key::LeftBracket), Key>);
    type.EnumValues.Add("LeftBracket");
    static_assert(std::is_same_v<decltype(Key::Backslash), Key>);
    type.EnumValues.Add("Backslash");
    static_assert(std::is_same_v<decltype(Key::RightBracket), Key>);
    type.EnumValues.Add("RightBracket");
    static_assert(std::is_same_v<decltype(Key::GraveAccent), Key>);
    type.EnumValues.Add("GraveAccent");
    static_assert(std::is_same_v<decltype(Key::CapsLock), Key>);
    type.EnumValues.Add("CapsLock");
    static_assert(std::is_same_v<decltype(Key::ScrollLock), Key>);
    type.EnumValues.Add("ScrollLock");
    static_assert(std::is_same_v<decltype(Key::NumLock), Key>);
    type.EnumValues.Add("NumLock");
    static_assert(std::is_same_v<decltype(Key::PrintScreen), Key>);
    type.EnumValues.Add("PrintScreen");
    static_assert(std::is_same_v<decltype(Key::Pause), Key>);
    type.EnumValues.Add("Pause");
    static_assert(std::is_same_v<decltype(Key::Keypad0), Key>);
    type.EnumValues.Add("Keypad0");
    static_assert(std::is_same_v<decltype(Key::Keypad1), Key>);
    type.EnumValues.Add("Keypad1");
    static_assert(std::is_same_v<decltype(Key::Keypad2), Key>);
    type.EnumValues.Add("Keypad2");
    static_assert(std::is_same_v<decltype(Key::Keypad3), Key>);
    type.EnumValues.Add("Keypad3");
    static_assert(std::is_same_v<decltype(Key::Keypad4), Key>);
    type.EnumValues.Add("Keypad4");
    static_assert(std::is_same_v<decltype(Key::Keypad5), Key>);
    type.EnumValues.Add("Keypad5");
    static_assert(std::is_same_v<decltype(Key::Keypad6), Key>);
    type.EnumValues.Add("Keypad6");
    static_assert(std::is_same_v<decltype(Key::Keypad7), Key>);
    type.EnumValues.Add("Keypad7");
    static_assert(std::is_same_v<decltype(Key::Keypad8), Key>);
    type.EnumValues.Add("Keypad8");
    static_assert(std::is_same_v<decltype(Key::Keypad9), Key>);
    type.EnumValues.Add("Keypad9");
    static_assert(std::is_same_v<decltype(Key::KeypadDecimal), Key>);
    type.EnumValues.Add("KeypadDecimal");
    static_assert(std::is_same_v<decltype(Key::KeypadDivide), Key>);
    type.EnumValues.Add("KeypadDivide");
    static_assert(std::is_same_v<decltype(Key::KeypadMultiply), Key>);
    type.EnumValues.Add("KeypadMultiply");
    static_assert(std::is_same_v<decltype(Key::KeypadSubtract), Key>);
    type.EnumValues.Add("KeypadSubtract");
    static_assert(std::is_same_v<decltype(Key::KeypadAdd), Key>);
    type.EnumValues.Add("KeypadAdd");
    static_assert(std::is_same_v<decltype(Key::KeypadEnter), Key>);
    type.EnumValues.Add("KeypadEnter");
    static_assert(std::is_same_v<decltype(Key::Count), Key>);
    type.EnumValues.Add("Count");
    }
    {
    auto& type = AddType(catalog, "GamepadButton", "JBro::GamepadButton", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(GamepadButton::South), GamepadButton>);
    type.EnumValues.Add("South");
    static_assert(std::is_same_v<decltype(GamepadButton::East), GamepadButton>);
    type.EnumValues.Add("East");
    static_assert(std::is_same_v<decltype(GamepadButton::West), GamepadButton>);
    type.EnumValues.Add("West");
    static_assert(std::is_same_v<decltype(GamepadButton::North), GamepadButton>);
    type.EnumValues.Add("North");
    static_assert(std::is_same_v<decltype(GamepadButton::LeftShoulder), GamepadButton>);
    type.EnumValues.Add("LeftShoulder");
    static_assert(std::is_same_v<decltype(GamepadButton::RightShoulder), GamepadButton>);
    type.EnumValues.Add("RightShoulder");
    static_assert(std::is_same_v<decltype(GamepadButton::Start), GamepadButton>);
    type.EnumValues.Add("Start");
    static_assert(std::is_same_v<decltype(GamepadButton::Select), GamepadButton>);
    type.EnumValues.Add("Select");
    static_assert(std::is_same_v<decltype(GamepadButton::DPadUp), GamepadButton>);
    type.EnumValues.Add("DPadUp");
    static_assert(std::is_same_v<decltype(GamepadButton::DPadDown), GamepadButton>);
    type.EnumValues.Add("DPadDown");
    static_assert(std::is_same_v<decltype(GamepadButton::DPadLeft), GamepadButton>);
    type.EnumValues.Add("DPadLeft");
    static_assert(std::is_same_v<decltype(GamepadButton::DPadRight), GamepadButton>);
    type.EnumValues.Add("DPadRight");
    static_assert(std::is_same_v<decltype(GamepadButton::LeftThumb), GamepadButton>);
    type.EnumValues.Add("LeftThumb");
    static_assert(std::is_same_v<decltype(GamepadButton::RightThumb), GamepadButton>);
    type.EnumValues.Add("RightThumb");
    static_assert(std::is_same_v<decltype(GamepadButton::Count), GamepadButton>);
    type.EnumValues.Add("Count");
    }
    {
    auto& type = AddType(catalog, "GamepadAxis", "JBro::GamepadAxis", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(GamepadAxis::LeftX), GamepadAxis>);
    type.EnumValues.Add("LeftX");
    static_assert(std::is_same_v<decltype(GamepadAxis::LeftY), GamepadAxis>);
    type.EnumValues.Add("LeftY");
    static_assert(std::is_same_v<decltype(GamepadAxis::RightX), GamepadAxis>);
    type.EnumValues.Add("RightX");
    static_assert(std::is_same_v<decltype(GamepadAxis::RightY), GamepadAxis>);
    type.EnumValues.Add("RightY");
    static_assert(std::is_same_v<decltype(GamepadAxis::LeftTrigger), GamepadAxis>);
    type.EnumValues.Add("LeftTrigger");
    static_assert(std::is_same_v<decltype(GamepadAxis::RightTrigger), GamepadAxis>);
    type.EnumValues.Add("RightTrigger");
    static_assert(std::is_same_v<decltype(GamepadAxis::Count), GamepadAxis>);
    type.EnumValues.Add("Count");
    }
    {
    auto& type = AddType(catalog, "TouchPhase", "JBro::TouchPhase", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(TouchPhase::Began), TouchPhase>);
    type.EnumValues.Add("Began");
    static_assert(std::is_same_v<decltype(TouchPhase::Moved), TouchPhase>);
    type.EnumValues.Add("Moved");
    static_assert(std::is_same_v<decltype(TouchPhase::Stationary), TouchPhase>);
    type.EnumValues.Add("Stationary");
    static_assert(std::is_same_v<decltype(TouchPhase::Ended), TouchPhase>);
    type.EnumValues.Add("Ended");
    static_assert(std::is_same_v<decltype(TouchPhase::Cancelled), TouchPhase>);
    type.EnumValues.Add("Cancelled");
    }
    {
    auto& type = AddType(catalog, "InputDevice", "JBro::InputDevice", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputDevice::Keyboard), InputDevice>);
    type.EnumValues.Add("Keyboard");
    static_assert(std::is_same_v<decltype(InputDevice::Mouse), InputDevice>);
    type.EnumValues.Add("Mouse");
    static_assert(std::is_same_v<decltype(InputDevice::Gamepad), InputDevice>);
    type.EnumValues.Add("Gamepad");
    static_assert(std::is_same_v<decltype(InputDevice::Touch), InputDevice>);
    type.EnumValues.Add("Touch");
    static_assert(std::is_same_v<decltype(InputDevice::Count), InputDevice>);
    type.EnumValues.Add("Count");
    }
    {
    auto& type = AddType(catalog, "InputActionType", "JBro::InputActionType", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputActionType::Bool), InputActionType>);
    type.EnumValues.Add("Bool");
    static_assert(std::is_same_v<decltype(InputActionType::Float), InputActionType>);
    type.EnumValues.Add("Float");
    static_assert(std::is_same_v<decltype(InputActionType::Vector2), InputActionType>);
    type.EnumValues.Add("Vector2");
    }
    {
    auto& type = AddType(catalog, "InputBindingSource", "JBro::InputBindingSource", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputBindingSource::Key), InputBindingSource>);
    type.EnumValues.Add("Key");
    static_assert(std::is_same_v<decltype(InputBindingSource::MouseButton), InputBindingSource>);
    type.EnumValues.Add("MouseButton");
    static_assert(std::is_same_v<decltype(InputBindingSource::GamepadButton), InputBindingSource>);
    type.EnumValues.Add("GamepadButton");
    static_assert(std::is_same_v<decltype(InputBindingSource::GamepadAxis), InputBindingSource>);
    type.EnumValues.Add("GamepadAxis");
    static_assert(std::is_same_v<decltype(InputBindingSource::GamepadStick), InputBindingSource>);
    type.EnumValues.Add("GamepadStick");
    }
    {
    auto& type = AddType(catalog, "InputComposite", "JBro::InputComposite", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputComposite::None), InputComposite>);
    type.EnumValues.Add("None");
    static_assert(std::is_same_v<decltype(InputComposite::Up), InputComposite>);
    type.EnumValues.Add("Up");
    static_assert(std::is_same_v<decltype(InputComposite::Down), InputComposite>);
    type.EnumValues.Add("Down");
    static_assert(std::is_same_v<decltype(InputComposite::Left), InputComposite>);
    type.EnumValues.Add("Left");
    static_assert(std::is_same_v<decltype(InputComposite::Right), InputComposite>);
    type.EnumValues.Add("Right");
    }
    {
    auto& type = AddType(catalog, "InputCaptureResult", "JBro::InputCaptureResult", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputCaptureResult::None), InputCaptureResult>);
    type.EnumValues.Add("None");
    static_assert(std::is_same_v<decltype(InputCaptureResult::Captured), InputCaptureResult>);
    type.EnumValues.Add("Captured");
    static_assert(std::is_same_v<decltype(InputCaptureResult::Cancelled), InputCaptureResult>);
    type.EnumValues.Add("Cancelled");
    }
    {
    auto& type = AddType(catalog, "InputResult", "JBro::InputResult", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(InputResult::Pass), InputResult>);
    type.EnumValues.Add("Pass");
    static_assert(std::is_same_v<decltype(InputResult::Block), InputResult>);
    type.EnumValues.Add("Block");
    }
    {
    auto& type = AddType(catalog, "NetworkRole", "JBro::Network::NetworkRole", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::NetworkRole::None), Network::NetworkRole>);
    type.EnumValues.Add("None");
    static_assert(std::is_same_v<decltype(Network::NetworkRole::Client), Network::NetworkRole>);
    type.EnumValues.Add("Client");
    static_assert(std::is_same_v<decltype(Network::NetworkRole::Server), Network::NetworkRole>);
    type.EnumValues.Add("Server");
    }
    {
    auto& type = AddType(catalog, "ConnectionState", "JBro::Network::ConnectionState", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::ConnectionState::Disconnected), Network::ConnectionState>);
    type.EnumValues.Add("Disconnected");
    static_assert(std::is_same_v<decltype(Network::ConnectionState::Connecting), Network::ConnectionState>);
    type.EnumValues.Add("Connecting");
    static_assert(std::is_same_v<decltype(Network::ConnectionState::Connected), Network::ConnectionState>);
    type.EnumValues.Add("Connected");
    }
    {
    auto& type = AddType(catalog, "DisconnectReason", "JBro::Network::DisconnectReason", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::DisconnectReason::Normal), Network::DisconnectReason>);
    type.EnumValues.Add("Normal");
    static_assert(std::is_same_v<decltype(Network::DisconnectReason::Timeout), Network::DisconnectReason>);
    type.EnumValues.Add("Timeout");
    static_assert(std::is_same_v<decltype(Network::DisconnectReason::VersionMismatch), Network::DisconnectReason>);
    type.EnumValues.Add("VersionMismatch");
    static_assert(std::is_same_v<decltype(Network::DisconnectReason::Error), Network::DisconnectReason>);
    type.EnumValues.Add("Error");
    }
    {
    auto& type = AddType(catalog, "NetChannel", "JBro::Network::NetChannel", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::NetChannel::ReliableOrdered), Network::NetChannel>);
    type.EnumValues.Add("ReliableOrdered");
    static_assert(std::is_same_v<decltype(Network::NetChannel::ReliableUnordered), Network::NetChannel>);
    type.EnumValues.Add("ReliableUnordered");
    static_assert(std::is_same_v<decltype(Network::NetChannel::Unreliable), Network::NetChannel>);
    type.EnumValues.Add("Unreliable");
    static_assert(std::is_same_v<decltype(Network::NetChannel::UnreliableSequenced), Network::NetChannel>);
    type.EnumValues.Add("UnreliableSequenced");
    }
    {
    auto& type = AddType(catalog, "ConnectionKind", "JBro::Network::ConnectionKind", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::ConnectionKind::Socket), Network::ConnectionKind>);
    type.EnumValues.Add("Socket");
    static_assert(std::is_same_v<decltype(Network::ConnectionKind::Peer), Network::ConnectionKind>);
    type.EnumValues.Add("Peer");
    }
    {
    auto& type = AddType(catalog, "NetworkEventKind", "JBro::Network::NetworkEventKind", ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(Network::NetworkEventKind::Connected), Network::NetworkEventKind>);
    type.EnumValues.Add("Connected");
    static_assert(std::is_same_v<decltype(Network::NetworkEventKind::Disconnected), Network::NetworkEventKind>);
    type.EnumValues.Add("Disconnected");
    static_assert(std::is_same_v<decltype(Network::NetworkEventKind::Overflow), Network::NetworkEventKind>);
    type.EnumValues.Add("Overflow");
    }
    {
    auto& type = AddType(catalog,"AssetType","JBro::AssetType",ApiTypeKind::Enum);
    static_assert(std::is_same_v<decltype(AssetType::Unknown),AssetType>);
    type.EnumValues.Add("Unknown");
    static_assert(std::is_same_v<decltype(AssetType::Texture),AssetType>);
    type.EnumValues.Add("Texture");
    static_assert(std::is_same_v<decltype(AssetType::Sprite),AssetType>);
    type.EnumValues.Add("Sprite");
    static_assert(std::is_same_v<decltype(AssetType::Mesh),AssetType>);
    type.EnumValues.Add("Mesh");
    static_assert(std::is_same_v<decltype(AssetType::Material),AssetType>);
    type.EnumValues.Add("Material");
    static_assert(std::is_same_v<decltype(AssetType::Shader),AssetType>);
    type.EnumValues.Add("Shader");
    static_assert(std::is_same_v<decltype(AssetType::Canvas),AssetType>);
    type.EnumValues.Add("Canvas");
    static_assert(std::is_same_v<decltype(AssetType::Prefab),AssetType>);
    type.EnumValues.Add("Prefab");
    static_assert(std::is_same_v<decltype(AssetType::Audio),AssetType>);
    type.EnumValues.Add("Audio");
    static_assert(std::is_same_v<decltype(AssetType::Font),AssetType>);
    type.EnumValues.Add("Font");
    static_assert(std::is_same_v<decltype(AssetType::FontFamily),AssetType>);
    type.EnumValues.Add("FontFamily");
    static_assert(std::is_same_v<decltype(AssetType::StringTable),AssetType>);
    type.EnumValues.Add("StringTable");
    static_assert(std::is_same_v<decltype(AssetType::Layer),AssetType>);
    type.EnumValues.Add("Layer");
    static_assert(std::is_same_v<decltype(AssetType::AudioEffect),AssetType>);
    type.EnumValues.Add("AudioEffect");
    }
    {
    auto& type = AddType(catalog,"SpriteSliceType","JBro::SpriteSliceType",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<SpriteSliceType>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog,"TextureFilter","JBro::TextureFilter",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<TextureFilter>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog,"AudioImportMode","JBro::AudioImportMode",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<AudioImportMode>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog,"FontRenderMode","JBro::FontRenderMode",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<FontRenderMode>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog,"FontPrewarm","JBro::FontPrewarm",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<FontPrewarm>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
    {
    auto& type = AddType(catalog,"AudioEffectKind","JBro::AudioEffectKind",ApiTypeKind::Enum);
    const auto& descriptor = TypeDescriptorOf<AudioEffectKind>::Get();
    for (UInt32 index = 0; index < descriptor.enumNames->count; ++index)
    {
        type.EnumValues.Add(descriptor.enumNames->names[index]);
    }
    }
}
}
