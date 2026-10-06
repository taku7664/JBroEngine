#include <JBro/Core/Yaml.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorShortcutManager.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 에디터 단축키 관리자(todo "에디터 공용 기반" 2 번, D-228).
//
// 앞 절은 ImGui 없이 등록·바꾸기·되돌리기·겹침·저장 모양을 재고, 뒤 절은 렌더러 없는 ImGui 컨텍스트에 키를 넣어
// 누름이 범위·막기·조합키·타자·게임 입력 규칙대로 가는지 잰다. 실제 에디터에서의 흐름은 `EditorApplicationTests` 에 있다.

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

    using JBro::EditorShortcutBinding;
    using JBro::EditorShortcutDesc;
    using JBro::EditorShortcutManager;
    using JBro::ShortcutHandle;

    EditorShortcutBinding Key(ImGuiKey key, JBro::Bool control = false, JBro::Bool shift = false, JBro::Bool alt = false)
    {
        return EditorShortcutBinding{key, control, shift, alt};
    }

    // 몇 번 불렸는지 센다. 할 수 있는지는 밖에서 바꾼다.
    class Counter final : public JBro::IEditorShortcutHandler
    {
    public:
        Counter(JBro::Int32& calls, const JBro::Bool* enabled = nullptr)
            : m_calls(calls), m_enabled(enabled)
        {
        }
        JBro::Bool CanExecute(const JBro::EditorApplication& editor) const override
        {
            (void)editor;
            return m_enabled == nullptr || *m_enabled;
        }
        const char* WhyBlocked(const JBro::EditorApplication& editor) const override
        {
            (void)editor;
            return "blocked for the test";
        }
        JBro::Bool Execute(JBro::EditorApplication& editor) override
        {
            (void)editor;
            ++m_calls;
            return true;
        }

    private:
        JBro::Int32& m_calls;
        const JBro::Bool* m_enabled = nullptr;
    };

    ShortcutHandle Add(EditorShortcutManager& shortcuts, const char* id, EditorShortcutBinding primary, JBro::Int32& calls,
        const char* scope = nullptr, EditorShortcutBinding secondary = {}, const JBro::Bool* enabled = nullptr)
    {
        EditorShortcutDesc desc;
        desc.id = id;
        desc.labelKey = id;
        desc.categoryKey = scope != nullptr ? scope : "global";
        desc.scope = scope;
        desc.primary = primary;
        desc.secondary = secondary;
        desc.handler = JBro::MakeOwnerPtr<Counter>(calls, enabled);
        return shortcuts.Register(std::move(desc));
    }

    // ── 등록·바꾸기 ─────────────────────────────────────────────────

    void TestRegistrationRefusesBadAndDuplicateNames()
    {
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        Check(Add(shortcuts, "a.b", Key(ImGuiKey_A), calls) != JBro::InvalidShortcutHandle, "a named shortcut must register");
        Check(Add(shortcuts, "a.b", Key(ImGuiKey_B), calls) == JBro::InvalidShortcutHandle, "the same name twice must be refused");
        Check(Add(shortcuts, "", Key(ImGuiKey_B), calls) == JBro::InvalidShortcutHandle, "an empty name must be refused");
        EditorShortcutDesc noHandler;
        noHandler.id = "no.handler";
        Check(shortcuts.Register(std::move(noHandler)) == JBro::InvalidShortcutHandle, "a shortcut that does nothing must be refused");
        Check(shortcuts.GetCount() == 1, "only the good one is in the table");
        Check(shortcuts.Find("a.b").primary == Key(ImGuiKey_A), "and it keeps its default");
        Check(shortcuts.Find("missing").handle == JBro::InvalidShortcutHandle, "an unknown name finds nothing");
    }

    void TestRemappingAndResetting()
    {
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        Add(shortcuts, "edit.redo", Key(ImGuiKey_Y, true), calls, nullptr, Key(ImGuiKey_Z, true, true));
        const JBro::UInt64 before = shortcuts.GetRevision();
        Check(shortcuts.SetBinding("edit.redo", 0, Key(ImGuiKey_R, true)), "remapping a known shortcut must work");
        JBro::EditorShortcutView view = shortcuts.Find("edit.redo");
        Check(view.primary == Key(ImGuiKey_R, true), "the new combination must stand");
        Check(view.secondary == Key(ImGuiKey_Z, true, true), "the other slot must be left alone");
        Check(view.defaultPrimary == Key(ImGuiKey_Y, true), "the default must be remembered");
        Check(view.customized, "it must say it was changed");
        Check(shortcuts.GetRevision() != before, "a change must move the revision so the editor saves");

        const JBro::UInt64 unchanged = shortcuts.GetRevision();
        Check(shortcuts.SetBinding("edit.redo", 0, Key(ImGuiKey_R, true)), "setting the same combination again is fine");
        Check(shortcuts.GetRevision() == unchanged, "but it is not a change");

        Check(shortcuts.SetBinding("edit.redo", 1, EditorShortcutBinding{}), "a slot can be cleared");
        Check(false == shortcuts.Find("edit.redo").secondary.IsSet(), "and then it is empty");
        Check(false == shortcuts.SetBinding("edit.redo", 2, Key(ImGuiKey_A)), "there are only two slots");
        Check(false == shortcuts.SetBinding("nope", 0, Key(ImGuiKey_A)), "an unknown name cannot be remapped");

        Check(shortcuts.ResetBinding("edit.redo"), "resetting a shortcut must work");
        view = shortcuts.Find("edit.redo");
        Check(view.primary == Key(ImGuiKey_Y, true) && view.secondary == Key(ImGuiKey_Z, true, true), "reset brings both defaults back");
        Check(false == view.customized, "and it is no longer customized");

        // 손으로 기본값으로 되돌린 것도 사용자의 것이 아니다 - 저장 목록에 남지 않는다.
        shortcuts.SetBinding("edit.redo", 0, Key(ImGuiKey_Q));
        shortcuts.SetBinding("edit.redo", 0, Key(ImGuiKey_Y, true));
        JBro::YamlWriter writer;
        shortcuts.Write(writer);
        Check(std::strstr(writer.GetText().c_str(), "edit.redo") == nullptr, "a shortcut set back to its default must not be saved");

        shortcuts.SetBinding("edit.redo", 0, Key(ImGuiKey_Q));
        shortcuts.ResetAll();
        Check(false == shortcuts.Find("edit.redo").customized, "reset all brings everything back");
    }

    // 등록을 풀어도 사람이 바꾼 키는 남는다 - 외부 에디터가 빠졌다 다시 붙어도 그대로다.
    void TestAnUnregisteredShortcutKeepsTheUsersKeys()
    {
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        const ShortcutHandle handle = Add(shortcuts, "tool.run", Key(ImGuiKey_F9), calls);
        shortcuts.SetBinding("tool.run", 0, Key(ImGuiKey_F10));
        shortcuts.Unregister(handle);
        Check(shortcuts.GetCount() == 0, "unregistering takes it off the table");
        JBro::YamlWriter writer;
        shortcuts.Write(writer);
        Check(std::strstr(writer.GetText().c_str(), "tool.run") != nullptr, "but the user's keys are still written");
        Add(shortcuts, "tool.run", Key(ImGuiKey_F9), calls);
        Check(shortcuts.Find("tool.run").primary == Key(ImGuiKey_F10), "and come back when it registers again");
    }

    // 저장한 글자를 다시 읽으면 같은 조합이 선다. 읽기 전에 등록된 것에도, 뒤에 등록되는 것에도.
    void TestPreferencesRoundTrip()
    {
        EditorShortcutManager saved;
        JBro::Int32 calls = 0;
        Add(saved, "edit.redo", Key(ImGuiKey_Y, true), calls, nullptr, Key(ImGuiKey_Z, true, true));
        Add(saved, "view.frame", Key(ImGuiKey_F), calls, "CanvasView");
        saved.SetBinding("edit.redo", 0, Key(ImGuiKey_Space, true, false, true));
        saved.SetBinding("edit.redo", 1, EditorShortcutBinding{});
        saved.SetBinding("view.frame", 0, Key(ImGuiKey_F1));
        JBro::YamlWriter writer;
        writer.WriteInt("Version", 1);
        saved.Write(writer);

        JBro::YamlDocument document;
        JBro::YamlError error;
        Check(document.Parse(writer.GetText().c_str(), writer.GetText().size(), error), "the written preferences must parse");

        EditorShortcutManager loaded;
        Add(loaded, "edit.redo", Key(ImGuiKey_Y, true), calls, nullptr, Key(ImGuiKey_Z, true, true));
        loaded.Read(document, document.GetRoot());
        const JBro::EditorShortcutView redo = loaded.Find("edit.redo");
        Check(redo.primary == Key(ImGuiKey_Space, true, false, true), "a registered shortcut takes the saved combination");
        Check(false == redo.secondary.IsSet(), "a cleared slot stays cleared");
        Add(loaded, "view.frame", Key(ImGuiKey_F), calls, "CanvasView");
        Check(loaded.Find("view.frame").primary == Key(ImGuiKey_F1), "one registered after reading takes it too");
    }

    // 모르는 키 이름이 섞인 줄은 통째로 버린다 - 반만 읽으면 사람이 적지 않은 조합이 선다.
    void TestAnUnreadableLineIsDropped()
    {
        const char text[] =
            "Version: 1\n"
            "Shortcuts:\n"
            "  edit.undo:\n"
            "    Primary: Ctrl+Hyperdrive\n"
            "    Secondary: Ctrl+U\n"
            "  edit.copy:\n"
            "    Primary: Ctrl+Shift+C\n";
        JBro::YamlDocument document;
        JBro::YamlError error;
        Check(document.Parse(text, sizeof(text) - 1, error), "the probe preferences must parse");
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        Add(shortcuts, "edit.undo", Key(ImGuiKey_Z, true), calls);
        Add(shortcuts, "edit.copy", Key(ImGuiKey_C, true), calls);
        shortcuts.Read(document, document.GetRoot());
        Check(shortcuts.Find("edit.undo").primary == Key(ImGuiKey_Z, true), "a line with an unknown key keeps the default");
        Check(false == shortcuts.Find("edit.undo").secondary.IsSet(), "even its readable slot is not taken");
        Check(shortcuts.Find("edit.copy").primary == Key(ImGuiKey_C, true, true), "the good line is read");
    }

    void TestDescribeAndParseAgree()
    {
        const EditorShortcutBinding cases[] = {
            Key(ImGuiKey_Space), Key(ImGuiKey_S, true), Key(ImGuiKey_F5, true, true, true), Key(ImGuiKey_Delete, false, true),
            Key(ImGuiKey_Keypad7), Key(ImGuiKey_Minus, true)};
        for (const EditorShortcutBinding& binding : cases)
        {
            const JBro::EditorShortcutText text = EditorShortcutManager::Describe(binding);
            EditorShortcutBinding parsed;
            Check(EditorShortcutManager::Parse(text.value, parsed), "every described combination must parse");
            Check(parsed == binding, "and come back the same");
        }
        Check(std::strcmp(EditorShortcutManager::Describe(Key(ImGuiKey_Space)).value, "Space") == 0,
            "the space bar reads Space");
        Check(std::strcmp(EditorShortcutManager::Describe(Key(ImGuiKey_Z, true, true)).value, "Ctrl+Shift+Z") == 0,
            "modifiers come first in a fixed order");
        EditorShortcutBinding parsed = Key(ImGuiKey_A);
        Check(EditorShortcutManager::Parse("", parsed) && false == parsed.IsSet(), "an empty text is an empty combination");
        Check(false == EditorShortcutManager::Parse("Hyper+A", parsed), "an unknown modifier is refused");
        Check(false == EditorShortcutManager::Parse("Ctrl+Nope", parsed), "an unknown key is refused");
        Check(false == EditorShortcutManager::Parse("Ctrl+LeftShift", parsed), "a modifier key alone is not a shortcut key");
        Check(false == EditorShortcutManager::Parse("MouseLeft", parsed), "nor is the left mouse button");
        Check(false == EditorShortcutManager::Parse("MouseRight", parsed), "nor the right one");
        Check(false == EditorShortcutManager::Parse("MouseMiddle", parsed), "nor the middle one");
        Check(false == EditorShortcutManager::Parse("GamepadStart", parsed), "nor a gamepad button");
        // 엄지 버튼만은 단축키가 된다(D-258). 설정 파일에 적힌 글자에서 그대로 돌아와야 한다.
        for (const EditorShortcutBinding& thumb : {Key(ImGuiKey_MouseX1), Key(ImGuiKey_MouseX2, true)})
        {
            const JBro::EditorShortcutText text = EditorShortcutManager::Describe(thumb);
            Check(EditorShortcutManager::Parse(text.value, parsed) && parsed == thumb,
                "a thumb button, with or without modifiers, reads back as itself");
        }
    }

    void TestConflictsAreFoundAndKindsDiffer()
    {
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        // 막지 않는 패널 것을 **맨 앞에** 둔다 - 겹침을 찾는 두 겹 반복에서 그것이 앞자리에도 뒷자리에도 서야 두 방향을 다 잰다.
        EditorShortcutDesc passes;
        passes.id = "p.passes";
        passes.scope = "Inspector";
        passes.primary = Key(ImGuiKey_K, true);
        passes.blocksGlobal = false;
        passes.handler = JBro::MakeOwnerPtr<Counter>(calls);
        shortcuts.Register(std::move(passes));
        Add(shortcuts, "g.one", Key(ImGuiKey_K, true), calls);
        Add(shortcuts, "g.two", Key(ImGuiKey_J), calls, nullptr, Key(ImGuiKey_K, true));
        Add(shortcuts, "p.view", Key(ImGuiKey_K, true), calls, "CanvasView");
        Add(shortcuts, "p.other", Key(ImGuiKey_K, true), calls, "Hierarchy");
        EditorShortcutDesc passesLate;
        passesLate.id = "p.passes_late";
        passesLate.scope = "Stats";
        passesLate.primary = Key(ImGuiKey_K, true);
        passesLate.blocksGlobal = false;
        passesLate.handler = JBro::MakeOwnerPtr<Counter>(calls);
        shortcuts.Register(std::move(passesLate));

        JBro::Array<JBro::ShortcutConflict> conflicts;
        shortcuts.FindConflicts(conflicts);
        JBro::Int32 clashes = 0;
        JBro::Int32 shadows = 0;
        for (const JBro::ShortcutConflict& conflict : conflicts)
        {
            const char* first = shortcuts.GetAt(conflict.first).id;
            const char* second = shortcuts.GetAt(conflict.second).id;
            Check(conflict.binding == Key(ImGuiKey_K, true), "every conflict here is on Ctrl+K");
            if (conflict.kind == JBro::ShortcutConflictKind::Clash)
            {
                ++clashes;
                Check(std::strcmp(first, "g.one") == 0 && std::strcmp(second, "g.two") == 0,
                    "two globals on one combination clash, even through a second slot");
            }
            else
            {
                ++shadows;
                Check(std::strncmp(first, "p.", 2) == 0 && std::strncmp(second, "g.", 2) == 0,
                    "a shadow names the panel one first and the global one second");
                Check(std::strncmp(first, "p.passes", 8) != 0, "a panel shortcut that lets the global one run shadows nothing");
            }
        }
        Check(clashes == 1, "exactly one clash");
        // 두 전역 × 두 패널(막는 것). 서로 다른 두 패널은 부딪히지 않는다.
        Check(shadows == 4, "each blocking panel shortcut shadows each global one");
    }

    // ── 누름 ──────────────────────────────────────────────────────

    class Stage
    {
    public:
        Stage()
        {
            m_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_context);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(400.0f, 300.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
            Frame();
        }
        ~Stage()
        {
            ImGui::DestroyContext(m_context);
        }
        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        // 한 프레임을 열고 닫는다. 키 이벤트는 여는 순간 들어간다.
        void Frame()
        {
            ImGui::NewFrame();
            ImGui::Render();
        }

        // 조합키를 누른 채 키 하나를 누르고, 그 프레임에 관리자를 돌린다. 실행한 수를 돌려준다.
        JBro::UInt32 Press(EditorShortcutManager& shortcuts, const EditorShortcutBinding& combo,
            JBro::Bool typing = false, JBro::Bool gameInput = false)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, combo.control);
            io.AddKeyEvent(ImGuiMod_Shift, combo.shift);
            io.AddKeyEvent(ImGuiMod_Alt, combo.alt);
            io.AddKeyEvent(combo.key, true);
            ImGui::NewFrame();
            const JBro::UInt32 executed = shortcuts.ProcessInput(m_editor, typing, gameInput);
            ImGui::Render();
            io.AddKeyEvent(combo.key, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            io.AddKeyEvent(ImGuiMod_Alt, false);
            Frame();
            return executed;
        }

        JBro::EditorApplication& Editor()
        {
            return m_editor;
        }

    private:
        ImGuiContext* m_context = nullptr;
        // 할 일이 에디터를 받지만 이 테스트의 할 일은 쓰지 않는다 - 띄우지 않은 에디터로 충분하다.
        JBro::EditorApplication m_editor;
    };

    void TestGlobalShortcutsFireAndModifiersAreExact()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 paste = 0;
        JBro::Int32 pasteChild = 0;
        Add(shortcuts, "edit.paste", Key(ImGuiKey_V, true), paste);
        Add(shortcuts, "edit.paste_child", Key(ImGuiKey_V, true, true), pasteChild);
        Check(stage.Press(shortcuts, Key(ImGuiKey_V, true)) == 1 && paste == 1 && pasteChild == 0,
            "Ctrl+V runs paste and not paste as child");
        Check(stage.Press(shortcuts, Key(ImGuiKey_V, true, true)) == 1 && paste == 1 && pasteChild == 1,
            "Ctrl+Shift+V runs only paste as child");
        Check(stage.Press(shortcuts, Key(ImGuiKey_V)) == 0, "plain V runs nothing");
        Check(stage.Press(shortcuts, Key(ImGuiKey_V, true, false, true)) == 0, "Ctrl+Alt+V runs nothing");

        shortcuts.SetBinding("edit.paste", 0, Key(ImGuiKey_P, true));
        Check(stage.Press(shortcuts, Key(ImGuiKey_V, true)) == 0, "after remapping the old combination is dead");
        Check(stage.Press(shortcuts, Key(ImGuiKey_P, true)) == 1 && paste == 2, "and the new one works");
    }

    void TestTheFocusedPanelGoesFirstAndBlocksTheGlobalOne()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 global = 0;
        JBro::Int32 canvas = 0;
        JBro::Int32 hierarchy = 0;
        Add(shortcuts, "g.delete", Key(ImGuiKey_Delete), global);
        Add(shortcuts, "canvas.delete", Key(ImGuiKey_Delete), canvas, "CanvasView");
        Add(shortcuts, "hierarchy.x", Key(ImGuiKey_X), hierarchy, "Hierarchy");

        Check(stage.Press(shortcuts, Key(ImGuiKey_Delete)) == 1 && global == 1 && canvas == 0,
            "with no panel focused only the global one runs");
        shortcuts.SetFocusedScope("CanvasView");
        Check(stage.Press(shortcuts, Key(ImGuiKey_Delete)) == 1 && canvas == 1 && global == 1,
            "with its panel focused the panel one runs and blocks the global one");
        Check(stage.Press(shortcuts, Key(ImGuiKey_X)) == 0 && hierarchy == 0, "another panel's shortcut does not run here");
        shortcuts.SetFocusedScope("Hierarchy");
        Check(stage.Press(shortcuts, Key(ImGuiKey_X)) == 1 && hierarchy == 1, "but does in its own panel");
        Check(stage.Press(shortcuts, Key(ImGuiKey_Delete)) == 1 && global == 2 && canvas == 1,
            "and there the global delete is back");
        shortcuts.SetFocusedScope(nullptr);
        Check(stage.Press(shortcuts, Key(ImGuiKey_X)) == 0, "losing focus turns panel shortcuts off");
    }

    void TestANonBlockingPanelShortcutLetsBothRun()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 global = 0;
        JBro::Int32 panel = 0;
        Add(shortcuts, "g.save", Key(ImGuiKey_S, true), global);
        EditorShortcutDesc desc;
        desc.id = "view.note";
        desc.scope = "CanvasView";
        desc.primary = Key(ImGuiKey_S, true);
        desc.blocksGlobal = false;
        desc.handler = JBro::MakeOwnerPtr<Counter>(panel);
        shortcuts.Register(std::move(desc));
        shortcuts.SetFocusedScope("CanvasView");
        Check(stage.Press(shortcuts, Key(ImGuiKey_S, true)) == 2 && global == 1 && panel == 1,
            "a panel shortcut that does not block lets the global one run too");
    }

    // 지금 못 하는 패널 단축키는 막지 않는다 - 그 키는 전역 것에게 간다.
    void TestAPanelShortcutThatCannotRunFallsThrough()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 global = 0;
        JBro::Int32 panel = 0;
        JBro::Bool enabled = false;
        Add(shortcuts, "g.delete", Key(ImGuiKey_Delete), global);
        Add(shortcuts, "canvas.delete", Key(ImGuiKey_Delete), panel, "CanvasView", {}, &enabled);
        shortcuts.SetFocusedScope("CanvasView");
        Check(stage.Press(shortcuts, Key(ImGuiKey_Delete)) == 1 && global == 1 && panel == 0,
            "a blocked panel shortcut lets the global one run");
        Check(shortcuts.WhyBlocked("canvas.delete", stage.Editor()) != nullptr,
            "and it can say why it is blocked");
        enabled = true;
        Check(stage.Press(shortcuts, Key(ImGuiKey_Delete)) == 1 && panel == 1 && global == 1, "once it can, it runs and blocks");
    }

    void TestTypingAndGameInputSilenceShortcutsUnlessAllowed()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 undo = 0;
        JBro::Int32 save = 0;
        JBro::Int32 play = 0;
        Add(shortcuts, "edit.undo", Key(ImGuiKey_Z, true), undo);
        EditorShortcutDesc saveDesc;
        saveDesc.id = "file.save";
        saveDesc.primary = Key(ImGuiKey_S, true);
        saveDesc.whileTyping = true;
        saveDesc.handler = JBro::MakeOwnerPtr<Counter>(save);
        shortcuts.Register(std::move(saveDesc));
        EditorShortcutDesc playDesc;
        playDesc.id = "sim.play";
        playDesc.primary = Key(ImGuiKey_F5);
        playDesc.duringGame = true;
        playDesc.handler = JBro::MakeOwnerPtr<Counter>(play);
        shortcuts.Register(std::move(playDesc));

        Check(stage.Press(shortcuts, Key(ImGuiKey_Z, true), true) == 0 && undo == 0, "typing silences undo");
        Check(stage.Press(shortcuts, Key(ImGuiKey_S, true), true) == 1 && save == 1, "but not a shortcut allowed while typing");
        Check(stage.Press(shortcuts, Key(ImGuiKey_Z, true), false, true) == 0 && undo == 0, "the game having the keys silences undo");
        Check(stage.Press(shortcuts, Key(ImGuiKey_S, true), false, true) == 0 && save == 1, "and save");
        Check(stage.Press(shortcuts, Key(ImGuiKey_F5), false, true) == 1 && play == 1, "but not play");
    }

    // **마우스 엄지 버튼도 단축키다**(D-258). 키매핑 칸이 잡고, 누르면 돈다. 칸을 누르는 왼쪽 버튼은 잡지 않는다 -
    // 잡으면 "키를 누르세요" 칸을 누른 그 손짓이 곧 새 조합이 된다.
    void TestAThumbButtonIsCapturedAndRunsAShortcut()
    {
        Stage stage;
        ImGuiIO& io = ImGui::GetIO();
        const auto press = [&](JBro::Int32 button) {
            io.AddMouseButtonEvent(button, true);
            ImGui::NewFrame();
        };
        const auto release = [&](JBro::Int32 button) {
            ImGui::Render();
            io.AddMouseButtonEvent(button, false);
            stage.Frame();
        };
        EditorShortcutBinding captured;
        press(0);
        Check(false == EditorShortcutManager::CaptureBinding(captured), "the left button is not captured");
        release(0);
        press(2);
        Check(false == EditorShortcutManager::CaptureBinding(captured), "nor the middle one");
        release(2);
        press(3);
        Check(EditorShortcutManager::CaptureBinding(captured) && captured == Key(ImGuiKey_MouseX1),
            "the back thumb button is captured as MouseX1");
        release(3);
        io.AddKeyEvent(ImGuiMod_Shift, true);
        press(4);
        Check(EditorShortcutManager::CaptureBinding(captured) && captured == Key(ImGuiKey_MouseX2, false, true),
            "the forward thumb button with Shift held is Shift+MouseX2");
        release(4);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        stage.Frame();

        EditorShortcutManager shortcuts;
        JBro::Int32 back = 0;
        Add(shortcuts, "view.back", Key(ImGuiKey_MouseX1), back);
        press(3);
        const JBro::UInt32 executed = shortcuts.ProcessInput(stage.Editor(), false, false);
        release(3);
        Check(executed == 1 && back == 1, "pressing the thumb button runs the shortcut bound to it");
        press(4);
        const JBro::UInt32 other = shortcuts.ProcessInput(stage.Editor(), false, false);
        release(4);
        Check(other == 0 && back == 1, "the other thumb button does not");
    }

    // 키매핑 칸이 누른 키를 조합으로 잡는다. 조합키만 누른 것은 잡지 않는다.
    void TestCapturingAKeyReadsTheCombination()
    {
        Stage stage;
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        ImGui::NewFrame();
        EditorShortcutBinding captured;
        Check(false == EditorShortcutManager::CaptureBinding(captured), "a modifier alone is not captured");
        ImGui::Render();
        io.AddKeyEvent(ImGuiKey_K, true);
        ImGui::NewFrame();
        Check(EditorShortcutManager::CaptureBinding(captured), "a key pressed with Ctrl held is captured");
        ImGui::Render();
        Check(captured == Key(ImGuiKey_K, true), "as Ctrl+K");
        io.AddKeyEvent(ImGuiKey_K, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        stage.Frame();
        io.AddKeyEvent(ImGuiKey_Space, true);
        ImGui::NewFrame();
        Check(EditorShortcutManager::CaptureBinding(captured) && captured == Key(ImGuiKey_Space), "the space bar alone is Space");
        Check(std::strcmp(EditorShortcutManager::Describe(captured).value, "Space") == 0, "and reads Space");
        ImGui::Render();
    }

    // 검색은 번역된 이름·무리·저장 이름·지금 조합 글자를 본다. 영문은 대소문자를 가리지 않는다.
    void TestSearchLooksAtNamesAndKeys()
    {
        EditorShortcutManager shortcuts;
        JBro::Int32 calls = 0;
        Add(shortcuts, "editor.undo", Key(ImGuiKey_Z, true), calls);
        shortcuts.SetBinding("editor.undo", 1, Key(ImGuiKey_Backspace, false, false, true));
        const JBro::EditorShortcutView view = shortcuts.Find("editor.undo");
        const char* label = "실행 취소";
        const char* category = "편집";
        Check(EditorShortcutManager::MatchesSearch("", label, category, view), "an empty search matches everything");
        Check(EditorShortcutManager::MatchesSearch(nullptr, label, category, view), "so does no search");
        Check(EditorShortcutManager::MatchesSearch("취소", label, category, view), "a piece of the translated name matches");
        Check(EditorShortcutManager::MatchesSearch("편집", label, category, view), "the category matches");
        Check(EditorShortcutManager::MatchesSearch("UNDO", label, category, view), "the saved name matches without case");
        Check(EditorShortcutManager::MatchesSearch("ctrl+z", label, category, view), "the first combination matches");
        Check(EditorShortcutManager::MatchesSearch("Alt+Back", label, category, view), "the second combination matches");
        Check(false == EditorShortcutManager::MatchesSearch("저장", label, category, view), "something else does not");
        Check(false == EditorShortcutManager::MatchesSearch("Ctrl+Y", label, category, view), "nor another combination");
    }

    // 키매핑 칸이 새 키를 잡는 동안은 아무 단축키도 돌지 않는다.
    void TestASuspendedManagerRunsNothing()
    {
        Stage stage;
        EditorShortcutManager shortcuts;
        JBro::Int32 save = 0;
        Add(shortcuts, "file.save", Key(ImGuiKey_S, true), save);
        shortcuts.SetSuspended(true);
        Check(shortcuts.IsSuspended(), "it says it is suspended");
        Check(stage.Press(shortcuts, Key(ImGuiKey_S, true)) == 0 && save == 0, "a suspended manager runs nothing");
        shortcuts.SetSuspended(false);
        Check(stage.Press(shortcuts, Key(ImGuiKey_S, true)) == 1 && save == 1, "and runs again once released");
    }
}

JBro::Int32 RunEditorShortcutTests()
{
    TestRegistrationRefusesBadAndDuplicateNames();
    TestRemappingAndResetting();
    TestAnUnregisteredShortcutKeepsTheUsersKeys();
    TestPreferencesRoundTrip();
    TestAnUnreadableLineIsDropped();
    TestDescribeAndParseAgree();
    TestAThumbButtonIsCapturedAndRunsAShortcut();
    TestConflictsAreFoundAndKindsDiffer();
    TestGlobalShortcutsFireAndModifiersAreExact();
    TestTheFocusedPanelGoesFirstAndBlocksTheGlobalOne();
    TestANonBlockingPanelShortcutLetsBothRun();
    TestAPanelShortcutThatCannotRunFallsThrough();
    TestTypingAndGameInputSilenceShortcutsUnlessAllowed();
    TestCapturingAKeyReadsTheCombination();
    TestSearchLooksAtNamesAndKeys();
    TestASuspendedManagerRunsNothing();
    std::cout << "Editor shortcut tests passed.\n";
    return 0;
}
