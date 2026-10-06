#include <JBro/Core/InputKeys.h>
#include <JBro/Core/Log.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Input/InputSystem.h>
#include <JBro/InputTypes/InputBuffer.h>
#include <JBro/InputTypes/InputRebinding.h>
#include <JBro/InputTypes/Service/InputService.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 입력 액션과 그 저장(D-214, input-plan §4 의 5).
namespace
{
    using namespace JBro;

    void Check(JBro::Bool condition, const char* message)
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
            JBro::Bool sawJump = false;
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

        // 표를 다시 넣으면 다시 말한다 - 고친 프로젝트에서 또 틀리면 알아야 한다. 경고를 기억한 표를 그대로 되넣어도 그렇다.
        input.SetActionMap(input.GetActionMap());
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
        const JBro::Bool parsed = ParseProjectFile(LegacyInput, std::strlen(LegacyInput), project, error);
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

    // 선입력과 코요테 타임(D-218). 60 fps 로 흘린다.
    void TestTheInputBufferRemembersASignalForAWhile()
    {
        constexpr JBro::Float dt = 1.0f / 60.0f;
        InputBuffer jump;
        Check(false == jump.Peek(1000.0f) && false == jump.Take(1000.0f), "a buffer that never saw a signal has nothing");
        jump.Feed(false, dt);
        Check(jump.age == InputBuffer::Never, "no signal does not start the clock");

        // 땅에 닿기 네 프레임 전에 눌렀다. 0.1 초 안이면 아직 뛴다.
        jump.Feed(true, dt);
        Check(jump.Peek(0.0f), "a signal this frame is inside any window");
        for (JBro::Int32 frame = 0; frame < 4; ++frame)
        {
            jump.Feed(false, dt);
        }
        Check(jump.Peek(0.1f) && false == jump.Peek(0.05f), "four frames later it is 67 ms old");
        Check(jump.Take(0.1f), "taking it inside the window gives it");
        Check(false == jump.Peek(1000.0f), "and a press is used once");

        // 땅을 떠난 뒤 여섯 프레임이면 0.1 초를 넘는다.
        InputBuffer ground;
        ground.Feed(true, dt);
        for (JBro::Int32 frame = 0; frame < 6; ++frame)
        {
            ground.Feed(false, dt);
        }
        Check(ground.Peek(0.11f) && false == ground.Peek(0.09f), "six frames after leaving the ground is 100 ms");
        ground.Feed(true, dt);
        Check(ground.Peek(0.0f), "touching again starts over");
        ground.Clear();
        Check(false == ground.Peek(1000.0f), "a cleared buffer has nothing");
        Check(false == ground.Take(1000.0f), "nothing to take either");

        // 뷰로 읽은 값을 넣으므로 위에서 가져간 누름은 버퍼에도 들어가지 않는다.
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Jump", InputActionType::Bool, {KeyBinding(Key::Space)});
        input.SetActionMap(map);
        const InputEvent down[] = { KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(down));
        struct Taker final : IInputHandler
        {
            InputResult OnInput(InputView&) override
            {
                return InputResult::Block;
            }
        } taker;
        input.BeginDispatch();
        input.Deliver(taker);
        input.EndDispatch();
        InputBuffer blocked;
        blocked.Feed(input.GetResidualView().IsActionPressed(MakeNameId("Jump")), dt);
        Check(false == blocked.Peek(1000.0f), "a press the UI blocked never reaches the buffer");
    }

    InputBinding PadBinding(GamepadButton button, JBro::Int32 pad)
    {
        InputBinding binding;
        binding.source = InputBindingSource::GamepadButton;
        binding.code = static_cast<std::uint16_t>(button);
        binding.gamepad = static_cast<std::int8_t>(pad);
        return binding;
    }

    // 게임이 바인딩을 바꾸고 되돌린다(D-218). 서비스로 부른다 - 스크립트가 보는 길 그대로다.
    void TestBindingsChangeAtRuntime()
    {
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Jump", InputActionType::Bool, {KeyBinding(Key::Space)});
        AddAction(map, "Fire", InputActionType::Bool, {KeyBinding(Key::F), KeyBinding(Key::G)});
        input.SetActionMap(map);
        BindInputSystemContext(input.GetSystemContext());
        const Service::InputService service;
        const InputActionId jump = MakeNameId("Jump");
        const InputActionId fire = MakeNameId("Fire");

        Check(service.GetActionBindingCount(jump) == 1, "the project's binding is there");
        InputBinding read;
        Check(service.GetActionBinding(jump, 0, read) && IsSameBinding(read, KeyBinding(Key::Space)), "and reads back");
        Check(false == service.GetActionBinding(jump, 1, read), "a place past the end has nothing");
        Check(service.SetActionBinding(jump, 0, KeyBinding(Key::Enter)), "a binding is replaced");
        const InputEvent enter[] = { KeyEvent(InputEventKind::KeyDown, Key::Enter), KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(enter));
        Check(input.GetResidualView().IsActionPressed(jump), "the new key presses the action at once");
        Check(service.SetActionBinding(jump, 1, PadBinding(GamepadButton::South, 1)) && service.GetActionBindingCount(jump) == 2,
            "the place after the last appends");
        Check(false == service.SetActionBinding(jump, 3, KeyBinding(Key::A)), "a place further out is refused");
        Check(false == service.SetActionBinding(MakeNameId("Nope"), 0, KeyBinding(Key::A)), "an unknown action is refused");
        for (JBro::UInt32 index = 2; index < MaxInputBindingsPerAction; ++index)
        {
            Check(service.SetActionBinding(jump, index, KeyBinding(Key::A)), "an action holds eight");
        }
        Check(false == service.SetActionBinding(jump, MaxInputBindingsPerAction, KeyBinding(Key::B)), "and no ninth");

        Check(service.RemoveActionBinding(fire, 0) && service.GetActionBindingCount(fire) == 1, "a binding is removed");
        Check(service.GetActionBinding(fire, 0, read) && IsSameBinding(read, KeyBinding(Key::G)), "and the one behind it moves up");
        Check(false == service.RemoveActionBinding(fire, 1), "removing past the end is refused");

        Check(service.ResetActionBindings(jump) && service.GetActionBindingCount(jump) == 1, "one action goes back");
        Check(service.GetActionBinding(jump, 0, read) && IsSameBinding(read, KeyBinding(Key::Space)), "to the project's binding");
        Check(service.GetActionBindingCount(fire) == 1, "the other action keeps its change");
        service.ResetAllActionBindings();
        Check(service.GetActionBindingCount(fire) == 2, "resetting all puts it back too");
        Check(false == service.ResetActionBindings(MakeNameId("Nope")), "an unknown action has nothing to reset");

        // 에디터가 재생을 멈출 때의 되돌리기에 리바인딩도 든다.
        service.SetActionBinding(jump, 0, KeyBinding(Key::Enter));
        input.ResetActions();
        Check(service.GetActionBinding(jump, 0, read) && IsSameBinding(read, KeyBinding(Key::Space)), "stopping play undoes a rebind");
        BindInputSystemContext({});
        Check(false == service.SetActionBinding(jump, 0, KeyBinding(Key::A)) && service.GetActionBindingCount(jump) == 0,
            "an unbound service does nothing");
    }

    // 키 설정 화면의 "다음에 누르는 키" 다.
    void TestTheNextPressIsCaptured()
    {
        System::InputSystem input;
        InputBinding captured;
        input.BeginFrame({});
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::None, "nothing pressed, nothing captured");

        const InputEvent keys[] = { KeyEvent(InputEventKind::KeyDown, Key::K), KeyEvent(InputEventKind::KeyDown, Key::B) };
        input.BeginFrame(View(keys));
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::Captured, "a new key press is captured");
        Check(captured.source == InputBindingSource::Key && captured.code == static_cast<std::uint16_t>(Key::B),
            "the lower-numbered key of two wins");
        Check(captured.gamepad == -1 && captured.composite == InputComposite::None, "with no pad and no direction");
        input.BeginFrame({});
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::None, "a key still held is not a new press");

        const InputEvent escape[] = { KeyEvent(InputEventKind::KeyDown, Key::Escape), KeyEvent(InputEventKind::KeyDown, Key::A) };
        input.BeginFrame(View(escape));
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::Cancelled, "Escape cancels, even with another key");

        const InputEvent click[] = { ButtonEvent(InputEventKind::MouseButtonDown, MouseButton::Right) };
        input.BeginFrame(View(click));
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::Captured
                && captured.source == InputBindingSource::MouseButton && captured.code == static_cast<std::uint16_t>(MouseButton::Right),
            "a mouse button is captured");

        input.BeginFrame({});
        GamepadRawState raw[MaxGamepads] = {};
        raw[2].connected = true;
        raw[2].buttons = static_cast<std::uint16_t>(1u << static_cast<std::uint32_t>(GamepadButton::East));
        raw[2].axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = 1.0f;
        input.FoldGamepads(raw);
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::Captured
                && captured.source == InputBindingSource::GamepadButton && captured.code == static_cast<std::uint16_t>(GamepadButton::East)
                && captured.gamepad == -1,
            "a pad button is captured for any pad");

        // 위의 레이어가 막은 누름은 잡지 않는다.
        const InputEvent key[] = { KeyEvent(InputEventKind::KeyDown, Key::Q) };
        input.BeginFrame(View(key));
        struct Blocker final : IInputHandler
        {
            InputResult OnInput(InputView&) override
            {
                return InputResult::Block;
            }
        } blocker;
        input.BeginDispatch();
        input.Deliver(blocker);
        input.EndDispatch();
        Check(CaptureBinding(input.GetResidualView(), captured) == InputCaptureResult::None, "a press the UI took is not captured");
        const InputBinding untouched = captured;
        Check(untouched.source == InputBindingSource::Key && untouched.code == 0, "and the output is left empty");
    }

    // 바꾼 것만 이름으로 적고, 읽으면 같은 바인딩이 된다.
    void TestBindingOverridesWriteAndReadAsText()
    {
        Log::Clear();
        System::InputSystem input;
        InputActionMap map;
        AddAction(map, "Jump", InputActionType::Bool, {KeyBinding(Key::Space)});
        AddAction(map, "Move", InputActionType::Vector2, {
            KeyBinding(Key::W, InputComposite::Up), KeyBinding(Key::S, InputComposite::Down)});
        AddAction(map, "Fire", InputActionType::Bool, {KeyBinding(Key::F)});
        AddAction(map, "Kept", InputActionType::Bool, {KeyBinding(Key::K)});
        input.SetActionMap(map);
        BindInputSystemContext(input.GetSystemContext());
        const Service::InputService service;

        String text;
        Check(service.WriteBindingOverrides(text) && text.empty(), "nothing changed, nothing written");

        service.SetActionBinding(MakeNameId("Jump"), 0, KeyBinding(Key::Enter));
        service.SetActionBinding(MakeNameId("Jump"), 1, PadBinding(GamepadButton::South, 1));
        service.SetActionBinding(MakeNameId("Move"), 0, KeyBinding(Key::I, InputComposite::Up));
        service.RemoveActionBinding(MakeNameId("Fire"), 0);
        Check(service.WriteBindingOverrides(text), "the changes are written");
        const char* expected =
            "Jump: \"Key Enter, GamepadButton South @1\"\n"
            "Move: \"Key I Up, Key S Down\"\n"
            "Fire: \"\"\n";
        if (text != expected)
        {
            std::cout << "  wrote:\n" << text.c_str();
        }
        Check(text == expected, "only the changed actions, by name, in the project's words");

        service.ResetAllActionBindings();
        Check(service.ReadBindingOverrides(text), "the text reads back");
        String again;
        Check(service.WriteBindingOverrides(again) && again == text, "and gives the same bindings");
        const InputEvent keys[] = { KeyEvent(InputEventKind::KeyDown, Key::I) };
        input.BeginFrame(View(keys));
        const InputVector2 move = input.GetResidualView().GetActionVector(MakeNameId("Move"));
        Check(move.y == 1.0f, "the read binding moves up");

        // 틀린 줄은 그 액션만 그대로 두고, 나머지는 읽는다. 옛 키 이름·주석·빈 줄·CRLF·지운 액션을 견딘다.
        service.ResetAllActionBindings();
        const String mixed(
            "# key settings\r\n"
            "\r\n"
            "Jump: \"Key Num1\"\r\n"
            "Move: \"Key NoSuchKey Up\"\r\n"
            "Removed: \"Key A\"\r\n"
            "Fire:\r\n"
            "Kept \"Key Z\"\r\n");
        Check(false == service.ReadBindingOverrides(mixed), "a text with a bad line says so");
        InputBinding read;
        Check(service.GetActionBinding(MakeNameId("Jump"), 0, read) && read.code == static_cast<std::uint16_t>(Key::Digit1),
            "a good line is read, old key names too");
        Check(service.GetActionBinding(MakeNameId("Move"), 0, read) && read.code == static_cast<std::uint16_t>(Key::W),
            "the action on a bad line keeps its bindings");
        Check(service.GetActionBindingCount(MakeNameId("Fire")) == 0, "an empty value clears the action");
        Check(service.GetActionBinding(MakeNameId("Kept"), 0, read) && read.code == static_cast<std::uint16_t>(Key::K),
            "a line without a colon changes nothing");
        Check(CountWarnings("line 4 of the binding overrides") == 1 && CountWarnings("line 7 of the binding overrides") == 1,
            "each bad line is reported by number");
        Check(CountWarnings("Removed") == 0, "an action the game no longer has is skipped quietly");

        const char* broken[] = {
            "Jump: \"Space\"",
            "Jump: \"Key Space Sideways\"",
            "Jump: \"Key Space @4\"",
            "Jump: \"Key Space @1 Up\"",
            "Jump: \"Key A, , Key B\"",
            "Jump: \"Key A, Key A, Key A, Key A, Key A, Key A, Key A, Key A, Key A\"",
            "Jump: \"GamepadStick Middle\"",
        };
        for (const char* line : broken)
        {
            service.ResetAllActionBindings();
            Check(false == service.ReadBindingOverrides(String(line)), "a malformed binding is refused");
            Check(service.GetActionBinding(MakeNameId("Jump"), 0, read) && read.code == static_cast<std::uint16_t>(Key::Space)
                    && service.GetActionBindingCount(MakeNameId("Jump")) == 1,
                "and the action keeps what it had");
        }
        Check(service.ReadBindingOverrides(String("Move: \"GamepadStick Right @0, Key W Up\"")), "a stick with a pad reads");
        Check(service.GetActionBinding(MakeNameId("Move"), 0, read) && read.source == InputBindingSource::GamepadStick
                && read.code == 1 && read.gamepad == 0, "as the right stick of pad 0");

        // 한 줄씩 따로 본다 - 다른 줄의 실패가 가리지 않게.
        service.ResetAllActionBindings();
        Check(false == service.ReadBindingOverrides(String("Kept \"Key Z\"")), "a line without a colon alone fails the read");
        Check(service.ReadBindingOverrides(String("# only a comment\nJump: \"Key J\"\n")), "a comment is not a bad line");
        Check(service.ReadBindingOverrides(String("Removed: \"Key A\"")), "an action the game dropped is not a failure");

        // 방향만, 패드만 바뀐 것도 바뀐 것이다. 되돌려 놓은 것은 바뀐 것이 아니다.
        service.ResetAllActionBindings();
        service.SetActionBinding(MakeNameId("Move"), 1, KeyBinding(Key::S, InputComposite::Left));
        service.SetActionBinding(MakeNameId("Fire"), 0, KeyBinding(Key::G));
        service.SetActionBinding(MakeNameId("Fire"), 0, KeyBinding(Key::F));
        service.SetActionBinding(MakeNameId("Kept"), 0, PadBinding(GamepadButton::South, 0));
        Check(service.WriteBindingOverrides(text), "writes");
        Check(text.find("Move: \"Key W Up, Key S Left\"") != String::npos, "a changed direction alone is written");
        Check(text.find("Fire") == String::npos, "an action set back to what it was is not written");
        Check(text.find("Kept: \"GamepadButton South @0\"") != String::npos, "a pad binding is written with its pad");
        InputBinding anyPad = PadBinding(GamepadButton::South, 0);
        anyPad.gamepad = -1;
        service.SetActionBinding(MakeNameId("Kept"), 0, anyPad);
        String anyText;
        Check(service.WriteBindingOverrides(anyText) && anyText.find("Kept: \"GamepadButton South\"") != String::npos,
            "and a change of pad alone is a change");

        // 호스트는 모자란 버퍼에 쓰지 않고 필요한 크기를 알린다.
        char small[4] = {'x', 'x', 'x', 'x'};
        std::size_t needed = 0;
        Check(false == input.WriteBindingOverrides(small, sizeof(small), needed) && needed == anyText.size(),
            "a buffer too small is refused with the size it needs");
        Check(small[0] == 'x', "and left as it was");
        BindInputSystemContext({});
    }

    void TestTheServiceCapturesFromTheResidualView()
    {
        System::InputSystem input;
        BindInputSystemContext(input.GetSystemContext());
        const Service::InputService service;
        const InputEvent key[] = { KeyEvent(InputEventKind::KeyDown, Key::M) };
        input.BeginFrame(View(key));
        InputBinding captured;
        Check(service.CaptureBinding(captured) == InputCaptureResult::Captured && captured.code == static_cast<std::uint16_t>(Key::M),
            "the service captures what the game sees");
        Check(false == IsSameBinding(PadBinding(GamepadButton::South, 0), PadBinding(GamepadButton::South, 1)),
            "two pads are two bindings");
        Check(false == IsSameBinding(KeyBinding(Key::W, InputComposite::Up), KeyBinding(Key::W, InputComposite::Down)),
            "two directions are two bindings");
        Check(IsSameBinding(PadBinding(GamepadButton::South, 0), PadBinding(GamepadButton::South, 0)), "the same is the same");
        BindInputSystemContext({});
        Check(service.CaptureBinding(captured) == InputCaptureResult::None, "an unbound service captures nothing");
    }

    ProjectInputAction ProjectAction(const char* name, const char* set, Key key)
    {
        ProjectInputAction action;
        action.name = name;
        action.set = set;
        ProjectInputBinding binding;
        binding.code = static_cast<std::uint16_t>(key);
        action.bindings.Add(binding);
        return action;
    }

    // 같은 E 가 걷기 세트에서는 타기, 차량 세트에서는 내리기다. 세트를 바꾸면 그 자리에서 뜻이 바뀐다.
    void TestActionSetsChooseWhatAKeyMeans()
    {
        Log::Clear();
        Array<ProjectInputAction> actions;
        actions.Add(ProjectAction("Jump", "", Key::Space));
        actions.Add(ProjectAction("Board", "Walk", Key::E));
        actions.Add(ProjectAction("Leave", "Vehicle", Key::E));
        actions.Add(ProjectAction("Honk", "Vehicle", Key::H));
        InputActionMap map;
        Check(MakeInputActionMap(actions, map), "four actions fit");
        Check(map.setCount == 3 && map.actions[3].set == map.actions[2].set, "two actions in one set share it");
        Check(map.actions[0].set == 0, "an action without a set is in Default");

        System::InputSystem input;
        input.SetActionMap(map);
        const NameId walk = MakeNameId("Walk");
        const NameId vehicle = MakeNameId("Vehicle");
        const InputActionId board = MakeNameId("Board");
        const InputActionId leave = MakeNameId("Leave");
        const InputEvent keys[] = { KeyEvent(InputEventKind::KeyDown, Key::E), KeyEvent(InputEventKind::KeyDown, Key::Space) };
        input.BeginFrame(View(keys));
        const InputView& view = input.GetResidualView();
        Check(view.IsActionPressed(MakeNameId("Jump")), "Default is on from the start");
        Check(false == view.IsActionDown(board) && false == view.IsActionDown(leave), "every other set starts off");
        Check(input.IsActionSetEnabled(DefaultInputActionSet) && false == input.IsActionSetEnabled(walk),
            "and says so");

        Check(input.SetActionSetEnabled(walk, true), "a set the project names turns on");
        Check(view.IsActionPressed(board) && false == view.IsActionDown(leave), "at once, in the same frame");
        Check(input.SetActionSetEnabled(walk, false) && input.SetActionSetEnabled(vehicle, true), "sets switch");
        Check(false == view.IsActionDown(board) && view.IsActionPressed(leave), "and the key means the other action");
        Check(false == view.IsActionDown(MakeNameId("Honk")), "a set only turns its actions on, it does not press them");

        Check(input.SetActionSetEnabled(DefaultInputActionSet, false), "Default can be turned off too");
        Check(false == view.IsActionDown(MakeNameId("Jump")), "and then its actions read zero");
        Check(CountWarnings("no input action \"") == 0, "an action in a turned off set is not an unknown action");

        // 없는 세트는 거절하고 한 번만 말한다 - 매 프레임 부르는 스크립트가 로그를 채우지 않는다.
        NameTable::Get().Intern("Boat");
        Check(false == input.SetActionSetEnabled(MakeNameId("Boat"), true), "a set the project does not have is refused");
        input.SetActionSetEnabled(MakeNameId("Boat"), true);
        Check(CountWarnings("\"Boat\"") == 1, "and reported once, by name");
        Check(false == input.IsActionSetEnabled(MakeNameId("Boat")), "an unknown set is never on");

        // 되돌리면 프로젝트의 처음 상태다. 경고 기억은 남는다.
        input.ResetActions();
        Check(input.IsActionSetEnabled(DefaultInputActionSet) && false == input.IsActionSetEnabled(vehicle),
            "a reset leaves only Default on");
        Check(view.IsActionDown(MakeNameId("Jump")) && false == view.IsActionDown(leave), "and the actions read that way");
        input.SetActionSetEnabled(MakeNameId("Boat"), true);
        Check(CountWarnings("\"Boat\"") == 1, "a reset does not repeat a warning");
        input.SetActionMap(map);
        input.SetActionSetEnabled(MakeNameId("Boat"), true);
        Check(CountWarnings("\"Boat\"") == 2, "a new action map does");
    }

    void TestActionSetsHaveALimit()
    {
        Array<ProjectInputAction> actions;
        actions.Add(ProjectAction("Named", "Default", Key::A));
        char name[16] = {};
        for (JBro::Int32 index = 0; index < 32; ++index)
        {
            std::snprintf(name, sizeof(name), "Set%d", index.Get());
            actions.Add(ProjectAction(name, name, Key::B));
        }
        actions.Add(ProjectAction("After", "Set0", Key::C));
        InputActionMap map;
        Check(false == MakeInputActionMap(actions, map), "a 33rd set does not fit and says so");
        Check(map.actions[0].set == 0, "writing Default is the same as writing nothing");
        Check(map.setCount == MaxInputActionSets, "the table holds 32 sets");
        Check(nullptr == map.Find(MakeNameId("Set31")), "the action whose set did not fit is left out, not moved to Default");
        const InputActionDesc* after = map.Find(MakeNameId("After"));
        Check(after != nullptr && after->set == 1, "the actions after it still come through");
    }

    void TestTheSetIsWrittenOnlyWhenNamed()
    {
        const char* text =
            "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n"
            "InputActions:\n"
            "  - Name: Leave\n"
            "    Type: Bool\n"
            "    Set: Vehicle\n"
            "    Bindings: []\n"
            "  - Name: Jump\n"
            "    Type: Bool\n"
            "    Bindings: []\n";
        ProjectFile project;
        ProjectFileError error;
        Check(ParseProjectFile(text, std::strlen(text), project, error), "a file with a set parses");
        Check(project.inputActions[0].set == "Vehicle" && project.inputActions[1].set.empty(), "the set comes through");
        String written;
        Check(WriteProjectFileText(project, text, std::strlen(text), written, error), "and writes");
        Check(written.compare(0, std::strlen(text), text) == 0, "back the same");
        project.inputActions[1].set = "Menu Screen";
        Check(WriteProjectFileText(project, text, std::strlen(text), written, error), "a changed set writes");
        ProjectFile reread;
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.inputActions[1].set == "Menu Screen",
            "a set name with a space comes back");
    }
}

JBro::Int32 RunInputActionTests()
{
    const JBro::Bool echo = Log::GetEchoToConsole();
    Log::SetEchoToConsole(false);
    TestEveryNameComesBack();
    TestBoolActionsCombineTheirButtons();
    TestVectorActionsComposeAndClamp();
    TestConsumedDevicesDropOutOfActions();
    TestAnUnknownActionReadsZeroAndWarnsOnce();
    TestTheLegacyInputBlocksRead();
    TestTheInputBlocksWriteBack();
    TestBadInputBlocksAreRefused();
    TestActionSetsChooseWhatAKeyMeans();
    TestTheInputBufferRemembersASignalForAWhile();
    TestBindingsChangeAtRuntime();
    TestTheNextPressIsCaptured();
    TestBindingOverridesWriteAndReadAsText();
    TestTheServiceCapturesFromTheResidualView();
    TestActionSetsHaveALimit();
    TestTheSetIsWrittenOnlyWhenNamed();
    Log::SetEchoToConsole(echo);
    std::cout << "Input action tests passed.\n";
    return 0;
}
