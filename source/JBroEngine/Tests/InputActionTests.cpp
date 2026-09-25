#include <JBro/Core/InputKeys.h>
#include <JBro/Core/Log.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Input/InputSystem.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 입력 액션과 그 저장(D-210, input-plan §4 의 5).
namespace
{
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    InputEvent KeyEvent(InputEventKind kind, Key key)
    {
        InputEvent event;
        event.kind = kind;
        event.key = key;
        return event;
    }

    InputEvent ButtonEvent(InputEventKind kind, MouseButton button)
    {
        InputEvent event;
        event.kind = kind;
        event.button = button;
        return event;
    }

    template<std::uint32_t Count>
    JArrayView<InputEvent> View(const InputEvent (&events)[Count])
    {
        return {events, Count};
    }

    InputBinding KeyBinding(Key key, InputComposite composite = InputComposite::None)
    {
        InputBinding binding;
        binding.source = InputBindingSource::Key;
        binding.code = static_cast<std::uint16_t>(key);
        binding.composite = composite;
        return binding;
    }

    void AddAction(InputActionMap& map, const char* name, InputActionType type,
        std::initializer_list<InputBinding> bindings)
    {
        InputActionDesc& desc = map.actions[map.count];
        desc.name = NameTable::Get().Intern(name);
        desc.type = type;
        for (const InputBinding& binding : bindings)
        {
            desc.bindings[desc.bindingCount] = binding;
            ++desc.bindingCount;
        }
        ++map.count;
    }

    // 이름 표가 열거자 차례를 따르는지 잰다. 개수는 static_assert 가 보지만 차례를 바꾸면 이 테스트만 운다.
    void TestEveryNameComesBack()
    {
        for (std::size_t index = 1; index < KeyCount; ++index)
        {
            const Key key = static_cast<Key>(index);
            Key found = Key::Unknown;
            Check(FindKeyByName(GetKeyName(key), found) && found == key, "every key name must lead back to its key");
        }
        Check(std::strcmp(GetKeyName(Key::Space), "Space") == 0, "a key's name is its enumerator");
        Check(std::strcmp(GetKeyName(Key::Digit0), "Digit0") == 0, "digits keep the new name");
        for (std::size_t index = 0; index < MouseButtonCount; ++index)
        {
            MouseButton found = MouseButton::Count;
            const MouseButton button = static_cast<MouseButton>(index);
            Check(FindMouseButtonByName(GetMouseButtonName(button), found) && found == button,
                "every mouse button name must lead back");
        }
        for (std::size_t index = 0; index < GamepadButtonCount; ++index)
        {
            GamepadButton found = GamepadButton::Count;
            const GamepadButton button = static_cast<GamepadButton>(index);
            Check(FindGamepadButtonByName(GetGamepadButtonName(button), found) && found == button,
                "every gamepad button name must lead back");
        }
        for (std::size_t index = 0; index < GamepadAxisCount; ++index)
        {
            GamepadAxis found = GamepadAxis::Count;
            const GamepadAxis axis = static_cast<GamepadAxis>(index);
            Check(FindGamepadAxisByName(GetGamepadAxisName(axis), found) && found == axis,
                "every gamepad axis name must lead back");
        }
        Check(std::strcmp(GetGamepadButtonName(GamepadButton::DPadLeft), "DPadLeft") == 0,
            "gamepad names are the old engine's names");

        // 기존 엔진의 옛 이름도 읽힌다. 옛 프로젝트의 바인딩이 그대로 열려야 한다.
        Key key = Key::Unknown;
        Check(FindKeyByName("Num7", key) && key == Key::Digit7, "Num7 is the old name of Digit7");
        Check(FindKeyByName("LeftCtrl", key) && key == Key::LeftControl, "LeftCtrl is LeftControl");
        Check(FindKeyByName("Numpad3", key) && key == Key::Keypad3, "Numpad3 is Keypad3");
        Check(FindKeyByName("NumpadEnter", key) && key == Key::KeypadEnter, "NumpadEnter is KeypadEnter");
        Check(FindKeyByName("Grave", key) && key == Key::GraveAccent, "Grave is GraveAccent");
        Check(false == FindKeyByName("NoSuchKey", key), "an unknown name is refused");
        Check(false == FindKeyByName(nullptr, key), "and so is no name");
    }

    // 두 키에 묶은 점프다. 한쪽만 떼면 아직 뗀 것이 아니다.
    void TestBoolActionsCombineTheirButtons()
    {
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Jump", InputActionType::Bool, {KeyBinding(Key::Space), KeyBinding(Key::W)});
        AddAction(map, "Fire", InputActionType::Bool, {[] {
            InputBinding binding;
            binding.source = InputBindingSource::MouseButton;
            binding.code = static_cast<std::uint16_t>(MouseButton::Left);
            return binding;
        }()});
        input.SetActionMap(map);
        const InputActionId jump = MakeNameId("Jump");

        const InputEvent both[] = { KeyEvent(InputEventKind::KeyDown, Key::Space), KeyEvent(InputEventKind::KeyDown, Key::W) };
        input.BeginFrame(View(both));
        const InputView& view = input.GetResidualView();
        Check(view.IsActionDown(jump) && view.IsActionPressed(jump), "a bound key presses the action");
        Check(view.GetActionFloat(jump) == 1.0f, "a held bool action reads 1");

        const InputEvent oneUp[] = { KeyEvent(InputEventKind::KeyUp, Key::Space) };
        input.BeginFrame(View(oneUp));
        Check(view.IsActionDown(jump), "the other key still holds the action");
        Check(false == view.IsActionReleased(jump), "so letting go of one key is not a release");

        const InputEvent allUp[] = { KeyEvent(InputEventKind::KeyUp, Key::W) };
        input.BeginFrame(View(allUp));
        Check(false == view.IsActionDown(jump) && view.IsActionReleased(jump), "letting go of the last key releases it");

        const InputEvent click[] = { ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Left) };
        input.BeginFrame(View(click));
        Check(view.IsActionPressed(MakeNameId("Fire")), "a mouse button binding presses its action");
    }

    // WASD 합성이다. 대각선은 길이 1 이다(기존 엔진과 같다).
    void TestVectorActionsComposeAndClamp()
    {
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Move", InputActionType::Vector2, {
            KeyBinding(Key::W, InputComposite::Up), KeyBinding(Key::S, InputComposite::Down),
            KeyBinding(Key::A, InputComposite::Left), KeyBinding(Key::D, InputComposite::Right)});
        input.SetActionMap(map);
        const InputActionId move = MakeNameId("Move");

        const InputEvent right[] = { KeyEvent(InputEventKind::KeyDown, Key::D) };
        input.BeginFrame(View(right));
        InputVector2 value = input.GetResidualView().GetActionVector(move);
        Check(value.x == 1.0f && value.y == 0.0f, "D moves right");

        const InputEvent up[] = { KeyEvent(InputEventKind::KeyDown, Key::W) };
        input.BeginFrame(View(up));
        value = input.GetResidualView().GetActionVector(move);
        Check(std::fabs(value.x * value.x + value.y * value.y - 1.0f) < 1.0e-5f && value.x > 0.0f && value.y > 0.0f,
            "a diagonal is clamped to length 1");

        const InputEvent left[] = { KeyEvent(InputEventKind::KeyDown, Key::A) };
        input.BeginFrame(View(left));
        value = input.GetResidualView().GetActionVector(move);
        Check(value.x == 0.0f && value.y == 1.0f, "left and right cancel");
        Check(input.GetResidualView().IsActionDown(move), "a nonzero vector is down");
    }

    // 소비는 액션에도 걸린다. 위에서 키보드를 가져가면 키보드로 묶은 액션은 아래에서 0 이다.
    void TestConsumedDevicesDropOutOfActions()
    {
        struct KeyboardTaker final : IInputHandler
        {
            InputResult OnInput(InputView& input) override
            {
                sawJump = input.IsActionDown(MakeNameId("Jump"));
                input.Consume(InputDevice::Keyboard);
                return InputResult::Pass;
            }
            bool sawJump = false;
        };
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Jump", InputActionType::Bool, {KeyBinding(Key::Space)});
        input.SetActionMap(map);
        const InputEvent down[] = { KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(down));

        KeyboardTaker taker;
        input.BeginDispatch();
        input.Deliver(taker);
        input.EndDispatch();
        Check(taker.sawJump, "the handler that took the keyboard still sees the action");
        Check(false == input.GetResidualView().IsActionDown(MakeNameId("Jump")),
            "below it the keyboard action is empty");
    }

    std::size_t CountWarnings(const char* needle)
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < Log::GetCount(); ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry != nullptr && std::strstr(entry->message, needle) != nullptr)
            {
                ++count;
            }
        }
        return count;
    }

    void TestAnUnknownActionReadsZeroAndWarnsOnce()
    {
        Log::Clear();
        System::InputSystem input;
        NameTable::Get().Intern("Teleport");
        const InputEvent down[] = { KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(down));
        const InputActionValue value = input.GetResidualView().Action(MakeNameId("Teleport"));
        Check(false == value.down && value.x == 0.0f, "an action the project does not have reads zero");
        input.GetResidualView().Action(MakeNameId("Teleport"));
        Check(CountWarnings("\"Teleport\"") == 1, "and it is reported once, by name");

        // 표를 다시 넣으면 다시 말한다 - 고친 프로젝트에서 또 틀리면 알아야 한다.
        input.SetActionMap(InputActionMap{});
        input.GetResidualView().Action(MakeNameId("Teleport"));
        Check(CountWarnings("\"Teleport\"") == 2, "a new action map forgets what it warned about");
    }

    // 기존 엔진이 쓰던 파일 모양이다(`TestProject/Test/Test.jproject` 에서 뽑았다).
    const char* const LegacyInput =
        "Version: 1\n"
        "EngineVersion: 0.1.0\n"
        "Framework: 2D\n"
        "InputLayers:\n"
        "  - Modal\n"
        "  - UI\n"
        "  - Game\n"
        "InputActions:\n"
        "  - Name: MoveLeft\n"
        "    Type: Bool\n"
        "    Bindings:\n"
        "      - Source: Key\n"
        "        Code: Left\n"
        "      - Source: GamepadButton\n"
        "        Code: DPadLeft\n"
        "  - Name: Move\n"
        "    Type: Vector2\n"
        "    Bindings:\n"
        "      - Source: Key\n"
        "        Code: W\n"
        "        Composite: Up\n"
        "      - Source: GamepadStick\n"
        "        Code: Left\n"
        "        GamepadIndex: 1\n"
        "  - Name: Pick\n"
        "    Type: Bool\n"
        "    Bindings:\n"
        "      - Source: Key\n"
        "        Code: Num1\n"
        "  - Name: Idle\n"
        "    Type: Float\n"
        "    Bindings: []\n"
        "AudioOutputDevice: \n";

    void TestTheLegacyInputBlocksRead()
    {
        ProjectFile project;
        ProjectFileError error;
        const bool parsed = ParseProjectFile(LegacyInput, std::strlen(LegacyInput), project, error);
        if (false == parsed)
        {
            std::cout << "  line " << error.line << ": " << error.message.c_str() << std::endl;
        }
        Check(parsed, "the old engine's input blocks must parse");
        Check(project.inputLayers.Size() == 3 && project.inputLayers[2] == "Game", "the layer order comes through");
        Check(project.inputActions.Size() == 4, "every action comes through");
        const ProjectInputAction& left = project.inputActions[0];
        Check(left.name == "MoveLeft" && left.type == InputActionType::Bool && left.bindings.Size() == 2,
            "an action keeps its name, type and bindings");
        Check(left.bindings[0].source == InputBindingSource::Key
            && left.bindings[0].code == static_cast<std::uint16_t>(Key::Left), "a key binding names its key");
        Check(left.bindings[1].source == InputBindingSource::GamepadButton
            && left.bindings[1].code == static_cast<std::uint16_t>(GamepadButton::DPadLeft), "and a pad binding its button");
        const ProjectInputAction& move = project.inputActions[1];
        Check(move.type == InputActionType::Vector2 && move.bindings[0].composite == InputComposite::Up,
            "a composite comes through");
        Check(move.bindings[1].source == InputBindingSource::GamepadStick && move.bindings[1].gamepad == 1,
            "a stick and its pad come through");
        Check(project.inputActions[2].bindings[0].code == static_cast<std::uint16_t>(Key::Digit1),
            "an old key name reads as the new key");
        Check(project.inputActions[3].bindings.IsEmpty(), "an empty binding list is an empty list");
        Check(project.audioOutputDevice.empty(), "the key after the blocks is still read");
    }

    void TestTheInputBlocksWriteBack()
    {
        ProjectFile project;
        ProjectFileError error;
        Check(ParseProjectFile(LegacyInput, std::strlen(LegacyInput), project, error), "the sample must parse");

        String once;
        Check(WriteProjectFileText(project, LegacyInput, std::strlen(LegacyInput), once, error), "the rewrite goes through");
        // 옛 이름은 새 이름으로 적힌다. 그 밖에는 원문과 같다.
        String expected(LegacyInput);
        const std::size_t at = expected.find("Code: Num1");
        Check(at != String::npos, "the sample holds the old name");
        expected.replace(at, std::strlen("Code: Num1"), "Code: Digit1");
        // 이 파일에 없던 최상위 키는 쓰기가 원래 뒤에 붙인다. 원문 몫만 견준다.
        Check(once.compare(0, expected.size(), expected) == 0,
            "an untouched file writes its lines back the same, with old key names renewed");

        // 고친 것이 파일에 가고, 다시 읽으면 같다.
        project.inputLayers.Clear();
        project.inputLayers.Add(String("Game"));
        project.inputActions[0].bindings[0].code = static_cast<std::uint16_t>(Key::A);
        project.inputActions[0].bindings[1].gamepad = 2;
        String edited;
        Check(WriteProjectFileText(project, once.c_str(), once.size(), edited, error), "the edited rewrite goes through");
        ProjectFile reread;
        Check(ParseProjectFile(edited.c_str(), edited.size(), reread, error), "the edited file reads back");
        Check(reread.inputLayers.Size() == 1 && reread.inputLayers[0] == "Game", "the new layer order is in the file");
        Check(reread.inputActions[0].bindings[0].code == static_cast<std::uint16_t>(Key::A), "the new key is in the file");
        Check(reread.inputActions[0].bindings[1].gamepad == 2, "the pad index is in the file");

        // 없던 블록은 비어 있으면 적지 않는다.
        const char* bare = "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n";
        ProjectFile plain;
        Check(ParseProjectFile(bare, std::strlen(bare), plain, error), "a bare file parses");
        String plainOut;
        Check(WriteProjectFileText(plain, bare, std::strlen(bare), plainOut, error), "and writes");
        Check(plainOut.find("Input") == String::npos, "a project without input settings gets none written");
        plain.inputActions.Emplace();
        plain.inputActions.Last().name = "Jump";
        Check(WriteProjectFileText(plain, bare, std::strlen(bare), plainOut, error), "a new action is written");
        Check(plainOut.find("InputActions:\n  - Name: Jump\n    Type: Bool\n    Bindings: []\n") != String::npos,
            "at the end, in the old engine's shape");
    }

    void TestBadInputBlocksAreRefused()
    {
        const char* cases[] =
        {
            "InputActions:\n  - Name: A\n    Bindings:\n      - Source: Keyboard\n",
            "InputActions:\n  - Name: A\n    Bindings:\n      - Source: Key\n        Code: NoSuchKey\n",
            "InputActions:\n  - Name: A\n    Type: Vector3\n",
            "InputActions:\n  - Name: A\n    Bindings:\n      - Source: GamepadButton\n        GamepadIndex: 7\n",
            "InputActions:\n  - Name: A\n    Bindings:\n      - Source: Key\n        Composite: Diagonal\n",
            "InputActions:\n    Type: Bool\n",
        };
        for (const char* body : cases)
        {
            String text("Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n");
            text.append(body);
            ProjectFile project;
            ProjectFileError error;
            Check(false == ParseProjectFile(text.c_str(), text.size(), project, error), "a broken input block is refused");
            Check(error.line > 0, "and the refusal names a line");
        }
    }
}

int RunInputActionTests()
{
    const bool echo = Log::GetEchoToConsole();
    Log::SetEchoToConsole(false);
    TestEveryNameComesBack();
    TestBoolActionsCombineTheirButtons();
    TestVectorActionsComposeAndClamp();
    TestConsumedDevicesDropOutOfActions();
    TestAnUnknownActionReadsZeroAndWarnsOnce();
    TestTheLegacyInputBlocksRead();
    TestTheInputBlocksWriteBack();
    TestBadInputBlocksAreRefused();
    Log::SetEchoToConsole(echo);
    std::cout << "Input action tests passed.\n";
    return 0;
}
