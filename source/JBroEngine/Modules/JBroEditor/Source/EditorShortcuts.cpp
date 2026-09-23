#include <JBro/Editor/EditorShortcuts.h>

#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <cstring>

namespace JBro::EditorShortcuts
{
    namespace
    {
        constexpr EditorShortcutBinding Bind(
            ImGuiKey key, bool control = false, bool shift = false)
        {
            return EditorShortcutBinding{key, control, shift};
        }

        // **기본 조합은 기존 엔진과 같다.** 쓰던 사람이 손으로 기억하는 값이라,
        // 바꿀 이유가 없으면 바꾸지 않는다.
        constexpr EditorShortcutInfo Table[] = {
            {EditorShortcut::SaveCanvas, LocKeys::MenuSaveCanvas, LocKeys::MenuFile,
                Bind(ImGuiKey_S, true), {}},
            {EditorShortcut::Undo, LocKeys::MenuUndo, LocKeys::MenuEdit,
                Bind(ImGuiKey_Z, true), {}},
            {EditorShortcut::Redo, LocKeys::MenuRedo, LocKeys::MenuEdit,
                Bind(ImGuiKey_Y, true), Bind(ImGuiKey_Z, true, true)},
            {EditorShortcut::Copy, LocKeys::HierarchyCopy, LocKeys::MenuEdit,
                Bind(ImGuiKey_C, true), {}},
            {EditorShortcut::Paste, LocKeys::HierarchyPaste, LocKeys::MenuEdit,
                Bind(ImGuiKey_V, true), {}},
            // Shift 를 정확히 견주므로 Ctrl+Shift+V 가 위의 Ctrl+V 를 오발동시키지 않는다(기존과 같다).
            {EditorShortcut::PasteAsChild, LocKeys::HierarchyPasteAsChild, LocKeys::MenuEdit,
                Bind(ImGuiKey_V, true, true), {}},
            {EditorShortcut::DeleteSelection, LocKeys::HierarchyDelete, LocKeys::MenuEdit,
                Bind(ImGuiKey_Delete), {}},
            {EditorShortcut::TogglePlay, LocKeys::MenuSimulationPlay, LocKeys::MenuSimulation,
                Bind(ImGuiKey_F5), {}},
            {EditorShortcut::TogglePause, LocKeys::MenuSimulationPause, LocKeys::MenuSimulation,
                Bind(ImGuiKey_F6), {}},
        };
        static_assert(
            sizeof(Table) / sizeof(Table[0]) == static_cast<std::size_t>(EditorShortcut::Count),
            "every shortcut needs a row, or Find returns the wrong one");

        void Append(char* buffer, std::size_t capacity, const char* text)
        {
            const std::size_t used = std::strlen(buffer);
            const std::size_t room = capacity - used - 1;
            const std::size_t length = std::strlen(text);
            std::memcpy(buffer + used, text, length < room ? length : room);
            buffer[used + (length < room ? length : room)] = '\0';
        }

        bool Pressed(const EditorShortcutBinding& binding)
        {
            if (false == binding.IsSet())
            {
                return false;
            }
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl != binding.control || io.KeyShift != binding.shift)
            {
                return false;
            }
            return ImGui::IsKeyPressed(binding.key, false);
        }
    }

    JArrayView<EditorShortcutInfo> All()
    {
        return {Table, static_cast<std::uint32_t>(sizeof(Table) / sizeof(Table[0]))};
    }

    const EditorShortcutInfo& Find(EditorShortcut id)
    {
        const std::size_t index = static_cast<std::size_t>(id);
        // 표는 열거 차례 그대로다(위의 static_assert 가 개수를 지킨다).
        return Table[index < static_cast<std::size_t>(EditorShortcut::Count) ? index : 0];
    }

    bool CanExecute(const EditorApplication& editor, EditorShortcut id)
    {
        // `GetCanvas` 는 const 가 아니다 - 캔버스를 내주는 길이 하나뿐이라 여기서만 벗긴다.
        EditorApplication& mutableEditor = const_cast<EditorApplication&>(editor);
        const bool hasCanvas = mutableEditor.GetCanvas() != nullptr;
        switch (id)
        {
        case EditorShortcut::SaveCanvas:
            // **돌고 있는 동안은 저장하지 않는다.** 게임이 만든 상태가 파일이 된다.
            return hasCanvas && false == editor.IsSimulationPlaying();
        case EditorShortcut::Undo:
            return mutableEditor.GetCommands().CanUndo();
        case EditorShortcut::Redo:
            return mutableEditor.GetCommands().CanRedo();
        case EditorShortcut::Copy:
            return editor.GetSelectionCount() != 0;
        case EditorShortcut::Paste:
            return hasCanvas && editor.HasClipboard();
        case EditorShortcut::PasteAsChild:
            // 자식으로 붙이려면 들어갈 곳이 있어야 한다.
            return hasCanvas && editor.HasClipboard() && editor.GetSelectedObject() != nullptr;
        case EditorShortcut::DeleteSelection:
            return editor.GetSelectionCount() != 0;
        case EditorShortcut::TogglePlay:
            return hasCanvas;
        case EditorShortcut::TogglePause:
            return editor.IsSimulationPlaying();
        default:
            return false;
        }
    }

    const char* WhyBlocked(const EditorApplication& editor, EditorShortcut id)
    {
        if (CanExecute(editor, id))
        {
            return nullptr;
        }
        EditorApplication& mutableEditor = const_cast<EditorApplication&>(editor);
        const bool hasCanvas = mutableEditor.GetCanvas() != nullptr;
        // 막은 조건이 여럿이면 **먼저 풀어야 하는 것**을 말한다. 프로젝트가 없는데
        // "시뮬레이션을 멈추세요" 라고 하면 멈출 것을 찾다 끝난다.
        if (false == hasCanvas
            && (id == EditorShortcut::SaveCanvas || id == EditorShortcut::Paste
                || id == EditorShortcut::PasteAsChild || id == EditorShortcut::TogglePlay))
        {
            return Loc::TextOr(LocKeys::BlockedNoProject, "no project is open");
        }
        switch (id)
        {
        case EditorShortcut::SaveCanvas:
            return Loc::TextOr(LocKeys::PopupSaveBlockedWhilePlaying,
                "stop the simulation before saving");
        case EditorShortcut::Undo:
            return Loc::TextOr(LocKeys::BlockedNothingToUndo, "there is nothing to undo");
        case EditorShortcut::Redo:
            return Loc::TextOr(LocKeys::BlockedNothingToRedo, "there is nothing to redo");
        case EditorShortcut::Copy:
        case EditorShortcut::DeleteSelection:
            return Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected");
        case EditorShortcut::Paste:
            return Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
        case EditorShortcut::PasteAsChild:
            // 붙일 것이 없는 것과 들어갈 곳이 없는 것은 다른 이야기다.
            if (false == editor.HasClipboard())
            {
                return Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
            }
            return Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected");
        case EditorShortcut::TogglePause:
            return Loc::TextOr(LocKeys::BlockedNotPlaying, "the simulation is not running");
        default:
            return nullptr;
        }
    }

    bool Execute(EditorApplication& editor, EditorShortcut id)
    {
        if (false == CanExecute(editor, id))
        {
            return false;
        }
        switch (id)
        {
        case EditorShortcut::SaveCanvas:
            editor.RequestSaveCanvas();
            return true;
        case EditorShortcut::Undo:
            return editor.GetCommands().Undo();
        case EditorShortcut::Redo:
            return editor.GetCommands().Redo();
        case EditorShortcut::Copy:
            return editor.CopySelection();
        case EditorShortcut::Paste:
            return editor.PasteClipboard();
        case EditorShortcut::PasteAsChild:
            return editor.PasteClipboard(true);
        case EditorShortcut::DeleteSelection:
            return EditorActions::DeleteSelection(editor);
        case EditorShortcut::TogglePlay:
            editor.ToggleSimulation();
            return true;
        case EditorShortcut::TogglePause:
            editor.SetSimulationPaused(false == editor.IsSimulationPaused());
            return true;
        default:
            return false;
        }
    }

    EditorShortcutText Describe(const EditorShortcutBinding& binding)
    {
        EditorShortcutText text;
        if (false == binding.IsSet())
        {
            return text;
        }
        if (binding.control)
        {
            Append(text.value, sizeof(text.value), "Ctrl+");
        }
        if (binding.shift)
        {
            Append(text.value, sizeof(text.value), "Shift+");
        }
        // ImGui 가 키 이름을 안다. 우리가 표를 또 만들면 둘이 갈린다.
        Append(text.value, sizeof(text.value), ImGui::GetKeyName(binding.key));
        return text;
    }

    EditorShortcutText Describe(EditorShortcut id)
    {
        return Describe(Find(id).primary);
    }

    void ProcessInput(EditorApplication& editor)
    {
        // **글자 칸이 입력을 먹고 있으면 건너뛴다.** 이름을 고치다 Ctrl+Z 를 누르면
        // 글자를 되돌려야지 씬을 되돌리면 안 된다.
        //
        // 저장은 예외다 - 글자를 치는 중에도 Ctrl+S 는 저장이어야 한다.
        const bool typing = ImGui::GetIO().WantTextInput;
        for (const EditorShortcutInfo& info : Table)
        {
            if (typing && info.id != EditorShortcut::SaveCanvas)
            {
                continue;
            }
            if (Pressed(info.primary) || Pressed(info.secondary))
            {
                Execute(editor, info.id);
                // 한 프레임에 하나다. 같은 키에 둘이 걸려 있으면 앞의 것이 이긴다.
                return;
            }
        }
    }
}
