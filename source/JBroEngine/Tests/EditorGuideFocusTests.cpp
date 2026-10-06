#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorGuideFocus.h>
#include <JBro/Editor/EditorShortcutManager.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 가이드 포커스 1 단계(D-251, `tasks/guide-focus-plan.md` §3): 모델과 입력 문.
//
// 앞 절은 **ImGui 없이** 거르기의 판단을 재고, 뒤 절은 실제 에디터가 거른 입력만 ImGui 에 넣는지 잰다.

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    using JBro::EditorGuideFocus;
    using JBro::GuideFocusPath;
    using JBro::GuideFocusTarget;
    using JBro::InputEvent;
    using JBro::InputEventKind;

    GuideFocusPath OneStepPath()
    {
        GuideFocusPath path;
        Check(path.Push({ JBro::MakeNameId("test.target"), 0 }), "a path must take its first target");
        return path;
    }

    InputEvent Move(JBro::Float x, JBro::Float y)
    {
        InputEvent event;
        event.kind = InputEventKind::MouseMove;
        event.x = x;
        event.y = y;
        return event;
    }

    InputEvent Button(InputEventKind kind, JBro::MouseButton button = JBro::MouseButton::Left)
    {
        InputEvent event;
        event.kind = kind;
        event.button = button;
        return event;
    }

    InputEvent KeyEvent(InputEventKind kind, JBro::Key key, JBro::Bool repeat = false)
    {
        InputEvent event;
        event.kind = kind;
        event.key = key;
        event.repeat = repeat;
        return event;
    }

    InputEvent Touch(InputEventKind kind, JBro::UInt32 pointer, JBro::Float x, JBro::Float y)
    {
        InputEvent event;
        event.kind = kind;
        event.codePoint = pointer;
        event.x = x;
        event.y = y;
        return event;
    }

    // 한 묶음을 거른다.
    JBro::Array<InputEvent> Filter(EditorGuideFocus& focus, std::initializer_list<InputEvent> events)
    {
        JBro::Array<InputEvent> in;
        for (const InputEvent& event : events)
        {
            in.Add(event);
        }
        JBro::Array<InputEvent> out;
        focus.FilterInput({ in.Data(), static_cast<JBro::UInt32>(in.Size()) }, out);
        return out;
    }

    JBro::Bool HasKind(const JBro::Array<InputEvent>& events, InputEventKind kind)
    {
        for (const InputEvent& event : events)
        {
            if (event.kind == kind)
            {
                return true;
            }
        }
        return false;
    }

    // 허용 영역: (100,100)~(200,150).
    const JBro::Rect Hole{ { 100.0f, 100.0f }, { 200.0f, 150.0f } };

    // ── 경로와 수명 ───────────────────────────────────────────────────

    void TestAPathRefusesWhatItCannotHold()
    {
        GuideFocusPath path;
        Check(false == path.Push({}), "a target with no name points at nothing and must be refused");
        for (JBro::UInt32 index = 0; index < GuideFocusPath::Capacity; ++index)
        {
            Check(path.Push({ JBro::MakeNameId("test.level"), index }), "a path takes up to its capacity");
        }
        Check(false == path.Push({ JBro::MakeNameId("test.level"), 99 }),
            "a full path must refuse the next target rather than drop a parent");
        Check(path.count == GuideFocusPath::Capacity, "and the refused one is not counted");

        EditorGuideFocus focus;
        Check(false == focus.Begin(GuideFocusPath{}), "an empty path has nowhere to lead");
        Check(false == focus.IsActive(), "and does not raise the veil");
        Check(focus.Begin(path), "a real path starts");
        Check(focus.IsActive() && focus.GetPath().count == path.count, "and is kept as given");
        focus.End();
        Check(false == focus.IsActive(), "ending lowers the veil");
    }

    void TestAllowedRectsStartEmptyAndHaveACapacity()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()), "the focus must start");
        Check(focus.GetAllowedRectCount() == 0, "nothing is allowed until the drawer says where the hole is");
        Check(false == focus.IsAllowed({ 150.0f, 120.0f }), "so not even the middle of the screen");
        for (JBro::UInt32 index = 0; index < EditorGuideFocus::AllowedRectCapacity; ++index)
        {
            Check(focus.AddAllowedRect(Hole), "the allowed area takes up to its capacity");
        }
        Check(false == focus.AddAllowedRect(Hole), "and refuses beyond it");
        focus.ClearAllowedRects();
        Check(focus.GetAllowedRectCount() == 0, "clearing empties it for the next frame");

        // 뒤집힌 사각형은 아무것도 담지 않는다 - 그리지 않은 위젯의 빈 사각형이 화면 한 점을 열어 두면 안 된다.
        Check(focus.AddAllowedRect({ { 200.0f, 200.0f }, { 100.0f, 100.0f } }), "an inverted rect can be added");
        Check(false == focus.IsAllowed({ 150.0f, 150.0f }), "but allows nothing");
        focus.ClearAllowedRects();
        Check(focus.AddAllowedRect(Hole), "the hole must be added");
        Check(focus.IsAllowed({ 100.0f, 100.0f }) && focus.IsAllowed({ 199.5f, 149.5f }),
            "the top left edge and the last pixel inside belong to the hole");
        Check(false == focus.IsAllowed({ 200.0f, 120.0f }) && false == focus.IsAllowed({ 150.0f, 150.0f }),
            "but the right and bottom edges are the next pixel over - a neighbour window lives there");

        // 다시 시작하면 지난 단계의 구멍을 들고 가지 않는다.
        Check(focus.Begin(OneStepPath()), "the focus must restart");
        Check(focus.GetAllowedRectCount() == 0, "a restart forgets the last step's hole");
    }

    // ── 입력 문 ───────────────────────────────────────────────────────

    void TestAnInactiveFocusPassesEverything()
    {
        EditorGuideFocus focus;
        const JBro::Array<InputEvent> out = Filter(focus, {
            Move(10.0f, 10.0f), Button(InputEventKind::MouseButtonDown), KeyEvent(InputEventKind::KeyDown, JBro::Key::A),
            KeyEvent(InputEventKind::KeyDown, JBro::Key::Escape) });
        Check(out.Size() == 4, "with the veil down every event goes through");
        Check(out[1].kind == InputEventKind::MouseButtonDown && out[3].key == JBro::Key::Escape,
            "unchanged and in order");
        Check(false == focus.ConsumeSkipRequest(), "and Esc is not taken as a skip");
    }

    void TestClicksOutsideTheHoleAreDropped()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");

        JBro::Array<InputEvent> out = Filter(focus, {
            Move(20.0f, 20.0f), Button(InputEventKind::MouseButtonDown), Button(InputEventKind::MouseButtonUp) });
        Check(out.Size() == 1 && out[0].kind == InputEventKind::MouseMove, "a click outside leaves only the move");
        Check(out[0].x == EditorGuideFocus::HiddenPointer && out[0].y == EditorGuideFocus::HiddenPointer,
            "and the move is sent off screen so nothing out there lights up as pressable");

        out = Filter(focus, { InputEvent{ InputEventKind::MouseWheel } });
        Check(out.Size() == 0, "a wheel outside is dropped");

        out = Filter(focus, {
            Move(150.0f, 120.0f), Button(InputEventKind::MouseButtonDown), Button(InputEventKind::MouseButtonUp) });
        Check(out.Size() == 3, "a click inside goes through whole");
        Check(out[0].x == 150.0f && out[0].y == 120.0f, "with the real position");

        InputEvent wheel;
        wheel.kind = InputEventKind::MouseWheel;
        wheel.y = 1.0f;
        out = Filter(focus, { wheel });
        Check(out.Size() == 1, "and a wheel inside goes through");
    }

    void TestADragStartedInsideKeepsItsRealPositionAndItsRelease()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        Filter(focus, { Move(150.0f, 120.0f), Button(InputEventKind::MouseButtonDown) });

        // 슬라이더를 끌다 밖으로 나갔다.
        JBro::Array<InputEvent> out = Filter(focus, { Move(400.0f, 300.0f) });
        Check(out.Size() == 1 && out[0].x == 400.0f && out[0].y == 300.0f,
            "a drag begun inside follows the real pointer outside - hiding it would freeze the slider");
        out = Filter(focus, { Button(InputEventKind::MouseButtonUp) });
        Check(out.Size() == 1 && out[0].kind == InputEventKind::MouseButtonUp,
            "and its release goes through even outside, or ImGui keeps the button held");

        // 끌기가 끝났으니 밖의 이동은 다시 숨는다.
        out = Filter(focus, { Move(410.0f, 300.0f) });
        Check(out.Size() == 1 && out[0].x == EditorGuideFocus::HiddenPointer, "once released, outside hides again");
    }

    void TestAButtonHeldBeforeTheVeilIsReleasedAfterIt()
    {
        EditorGuideFocus focus;
        // 꺼진 동안 누른 것 - ImGui 는 이미 누름을 봤다.
        Filter(focus, { Move(20.0f, 20.0f), Button(InputEventKind::MouseButtonDown, JBro::MouseButton::Right) });
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        const JBro::Array<InputEvent> out = Filter(focus, { Button(InputEventKind::MouseButtonUp, JBro::MouseButton::Right) });
        Check(out.Size() == 1 && out[0].button == JBro::MouseButton::Right,
            "a button pressed before the veil rose must still be released through it");

        // 같은 버튼을 다시 밖에서 누르고 떼면 둘 다 막힌다 - 앞의 뗌이 그 비트를 지웠다.
        const JBro::Array<InputEvent> again = Filter(focus, {
            Button(InputEventKind::MouseButtonDown, JBro::MouseButton::Right),
            Button(InputEventKind::MouseButtonUp, JBro::MouseButton::Right) });
        Check(again.Size() == 0, "after that release, the same button outside is blocked again");
    }

    void TestAReleaseWithoutAPassedPressIsDropped()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        // 밖에서 눌러 안으로 끌고 와서 뗐다. ImGui 는 누름을 모른다.
        const JBro::Array<InputEvent> out = Filter(focus, {
            Move(20.0f, 20.0f), Button(InputEventKind::MouseButtonDown), Move(150.0f, 120.0f),
            Button(InputEventKind::MouseButtonUp) });
        Check(false == HasKind(out, InputEventKind::MouseButtonDown), "the press outside is dropped");
        Check(false == HasKind(out, InputEventKind::MouseButtonUp),
            "and so is its release inside - a release without a press is a click ImGui never saw begin");
    }

    void TestKeysAreHeldBackUnlessTheStepTypes()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()), "the focus must start");
        InputEvent text;
        text.kind = InputEventKind::Text;
        text.codePoint = 'a';
        JBro::Array<InputEvent> out = Filter(focus, {
            KeyEvent(InputEventKind::KeyDown, JBro::Key::A), text, KeyEvent(InputEventKind::KeyUp, JBro::Key::A) });
        Check(out.Size() == 1 && out[0].kind == InputEventKind::KeyUp,
            "presses and text are held back; releases always go through so no key stays down");

        focus.SetKeyboardAllowed(true);
        out = Filter(focus, { KeyEvent(InputEventKind::KeyDown, JBro::Key::A), text });
        Check(out.Size() == 2, "a step that types lets presses and text through");

        Check(focus.Begin(OneStepPath()), "the focus must restart");
        Check(false == focus.IsKeyboardAllowed(), "a new path starts with the keyboard held back again");
    }

    void TestEscapeAsksToSkipOnce()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()), "the focus must start");
        focus.SetKeyboardAllowed(true);
        JBro::Array<InputEvent> out = Filter(focus, { KeyEvent(InputEventKind::KeyDown, JBro::Key::Escape) });
        Check(out.Size() == 0, "Esc is the veil's own key and is not handed on, even when typing is allowed");
        Check(focus.ConsumeSkipRequest(), "it asks to skip");
        Check(false == focus.ConsumeSkipRequest(), "once");

        out = Filter(focus, { KeyEvent(InputEventKind::KeyDown, JBro::Key::Escape, true) });
        Check(false == focus.ConsumeSkipRequest(), "a held Esc repeating does not ask again");
    }

    void TestFocusEventsAlwaysPassAndLosingFocusForgetsHeldButtons()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        Filter(focus, { Move(150.0f, 120.0f), Button(InputEventKind::MouseButtonDown) });
        JBro::Array<InputEvent> out = Filter(focus, { InputEvent{ InputEventKind::FocusLost } });
        Check(out.Size() == 1 && out[0].kind == InputEventKind::FocusLost, "losing the window's focus always goes through");
        // ImGui 는 포커스를 잃을 때 버튼을 다 뗀다. 그 뒤 창 밖에서 뗀 것이 오면 넘길 누름이 없다.
        out = Filter(focus, { Move(20.0f, 20.0f), Button(InputEventKind::MouseButtonUp) });
        Check(false == HasKind(out, InputEventKind::MouseButtonUp), "and after it no press is still counted as passed");
        Check(out.Size() == 1 && out[0].x == EditorGuideFocus::HiddenPointer, "so the pointer outside hides again");
    }

    void TestTouchesFollowTheSameRules()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        JBro::Array<InputEvent> out = Filter(focus, {
            Touch(InputEventKind::TouchBegan, 1, 20.0f, 20.0f), Touch(InputEventKind::TouchMoved, 1, 150.0f, 120.0f),
            Touch(InputEventKind::TouchEnded, 1, 150.0f, 120.0f) });
        Check(out.Size() == 0, "a touch that began outside is dropped whole, wherever it ends");

        out = Filter(focus, {
            Touch(InputEventKind::TouchBegan, 2, 150.0f, 120.0f), Touch(InputEventKind::TouchMoved, 2, 400.0f, 300.0f),
            Touch(InputEventKind::TouchCancelled, 2, 400.0f, 300.0f) });
        Check(out.Size() == 3, "a touch that began inside is followed out and its end goes through");

        out = Filter(focus, { Touch(InputEventKind::TouchBegan, 40, 150.0f, 120.0f) });
        Check(out.Size() == 0, "a pointer number the gate cannot count is not let in, so its end can never go missing");
    }

    void TestEndingBringsThePointerBack()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(OneStepPath()) && focus.AddAllowedRect(Hole), "the focus must start with a hole");
        Filter(focus, { Move(20.0f, 20.0f) });
        focus.End();
        JBro::Array<InputEvent> out = Filter(focus, {});
        Check(out.Size() == 1 && out[0].kind == InputEventKind::MouseMove && out[0].x == 20.0f && out[0].y == 20.0f,
            "the first frame after the veil falls puts the hidden pointer back where it really is");
        out = Filter(focus, {});
        Check(out.Size() == 0, "and only once");
    }

    // ── 실제 에디터 ───────────────────────────────────────────────────

    HWND FindOwnEditorWindow()
    {
        struct Search
        {
            HWND found = nullptr;
            DWORD process = GetCurrentProcessId();
        } search;
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                Search& search = *reinterpret_cast<Search*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (owner != search.process)
                {
                    return TRUE;
                }
                wchar_t className[64] = {};
                wchar_t title[64] = {};
                GetClassNameW(hwnd, className, 64);
                GetWindowTextW(hwnd, title, 64);
                if (std::wcscmp(className, L"JBroEngineWindow") == 0 && std::wcscmp(title, L"JBro Editor") == 0)
                {
                    search.found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    class CountingShortcut final : public JBro::IEditorShortcutHandler
    {
    public:
        explicit CountingShortcut(JBro::Int32& calls)
            : m_calls(calls)
        {
        }
        JBro::Bool Execute(JBro::EditorApplication& editor) override
        {
            (void)editor;
            ++m_calls;
            return true;
        }

    private:
        JBro::Int32& m_calls;
    };

    void TestTheEditorFeedsImGuiOnlyWhatTheGateLetsThrough()
    {
        constexpr JBro::Float Frame = 1.0f / 60.0f;
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the editor's guide focus gate not verified" << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({ 64, 48 }), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be found");
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        JBro::Int32 calls = 0;
        JBro::EditorShortcutDesc desc;
        desc.id = "test.guide_focus_probe";
        desc.labelKey = desc.id;
        desc.categoryKey = "global";
        desc.primary.key = ImGuiKey_F9;
        desc.handler = JBro::MakeOwnerPtr<CountingShortcut>(calls);
        const JBro::ShortcutHandle probe = editor.GetShortcuts().Register(std::move(desc));
        Check(probe != JBro::InvalidShortcutHandle, "the probe shortcut must register");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_F9, static_cast<LPARAM>(0xC0000001u));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(calls == 1, "with the veil down the probe shortcut runs, or the check below proves nothing");

        // 대상은 인스펙터 창 전체다. 에디터가 그 창을 그리며 자리를 알리고, 다음 프레임부터 그 안만 누를 수 있다.
        JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
        JBro::GuideFocusPath path;
        Check(path.Push(JBro::GuideFocusTargets::Panel("Inspector")), "the path must take the inspector");
        Check(focus.Begin(path), "the guide focus must start");
        Check(editor.Tick(Frame) && editor.Tick(Frame), "the editor must tick");
        const ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        const JBro::Int32 insideX = static_cast<JBro::Int32>(inspector->Pos.x + inspector->Size.x * 0.5f);
        const JBro::Int32 insideY = static_cast<JBro::Int32>(inspector->Pos.y + inspector->Size.y * 0.5f);
        Check(focus.IsAllowed({ static_cast<JBro::Float>(insideX), static_cast<JBro::Float>(insideY) }),
            "the inspector the editor drew is the allowed area");
        Check(false == focus.IsAllowed({ 40.0f, 40.0f }), "and the top left of the editor is not");

        // 밖을 누르면 ImGui 는 누름도 마우스도 보지 못한다.
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(40, 40));
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(false == ImGui::GetIO().MouseDown[0], "a press outside the hole never reaches ImGui");
        Check(false == ImGui::IsMousePosValid(), "and the pointer outside is hidden from it");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
        Check(editor.Tick(Frame), "the editor must tick");

        // 안을 누르면 들어간다.
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(insideX, insideY));
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(insideX, insideY));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(ImGui::GetIO().MouseDown[0], "a press inside the hole reaches ImGui");
        Check(ImGui::GetIO().MousePos.x == static_cast<JBro::Float>(insideX) && ImGui::GetIO().MousePos.y == static_cast<JBro::Float>(insideY),
            "at the real position");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(insideX, insideY));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(false == ImGui::GetIO().MouseDown[0], "and its release too");

        // 키를 허용한 단계에서도 에디터 단축키는 돌지 않는다.
        focus.SetKeyboardAllowed(true);
        PostMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        Check(ImGui::IsKeyDown(ImGuiKey_F9), "a step that types hands the key to ImGui");
        PostMessageW(hwnd, WM_KEYUP, VK_F9, static_cast<LPARAM>(0xC0000001u));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(calls == 1, "but no editor shortcut runs while the veil is up");

        // Esc 로 걷는다. 걷은 다음 프레임에는 마우스가 제자리로 돌아온다.
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(40, 40));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(false == ImGui::IsMousePosValid(), "the pointer is hidden again outside");
        PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_ESCAPE, static_cast<LPARAM>(0xC0000001u));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(false == focus.IsActive(), "Esc lowers the veil");
        Check(ImGui::IsMousePosValid() && ImGui::GetIO().MousePos.x == 40.0f,
            "and the pointer comes back where it is without the user having to move it");

        PostMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_F9, static_cast<LPARAM>(0xC0000001u));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(calls == 2, "with the veil down the shortcuts run again");
        editor.GetShortcuts().Unregister(probe);
    }
}

JBro::Int32 RunEditorGuideFocusTests()
{
    TestAPathRefusesWhatItCannotHold();
    TestAllowedRectsStartEmptyAndHaveACapacity();
    TestAnInactiveFocusPassesEverything();
    TestClicksOutsideTheHoleAreDropped();
    TestADragStartedInsideKeepsItsRealPositionAndItsRelease();
    TestAButtonHeldBeforeTheVeilIsReleasedAfterIt();
    TestAReleaseWithoutAPassedPressIsDropped();
    TestKeysAreHeldBackUnlessTheStepTypes();
    TestEscapeAsksToSkipOnce();
    TestFocusEventsAlwaysPassAndLosingFocusForgetsHeldButtons();
    TestTouchesFollowTheSameRules();
    TestEndingBringsThePointerBack();
    TestTheEditorFeedsImGuiOnlyWhatTheGateLetsThrough();
    std::cout << "Editor guide focus tests passed.\n";
    return 0;
}
