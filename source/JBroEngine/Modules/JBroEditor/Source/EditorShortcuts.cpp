#include <JBro/Editor/EditorShortcuts.h>

#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>


namespace JBro::EditorShortcuts
{
    namespace
    {
        constexpr EditorShortcutBinding Bind(
            ImGuiKey key, bool control = false, bool shift = false)
        {
            return EditorShortcutBinding{key, control, shift, false};
        }

        struct BuiltinRow
        {
            EditorShortcut id;
            const char* actionId;
            const char* labelKey;
            const char* categoryKey;
            EditorShortcutBinding primary;
            EditorShortcutBinding secondary;
        };

        // **기본 조합은 기존 엔진과 같다.** 쓰던 사람이 손으로 기억하는 값이라,
        // 바꿀 이유가 없으면 바꾸지 않는다. 사용자가 바꾼 것은 관리자가 이름(`actionId`)으로 덮는다.
        constexpr BuiltinRow Table[] = {
            {EditorShortcut::SaveCanvas, "editor.save_canvas", LocKeys::MenuSaveCanvas, LocKeys::MenuFile,
                Bind(ImGuiKey_S, true), {}},
            {EditorShortcut::Undo, "editor.undo", LocKeys::MenuUndo, LocKeys::MenuEdit,
                Bind(ImGuiKey_Z, true), {}},
            {EditorShortcut::Redo, "editor.redo", LocKeys::MenuRedo, LocKeys::MenuEdit,
                Bind(ImGuiKey_Y, true), Bind(ImGuiKey_Z, true, true)},
            {EditorShortcut::Copy, "editor.copy", LocKeys::HierarchyCopy, LocKeys::MenuEdit,
                Bind(ImGuiKey_C, true), {}},
            {EditorShortcut::Paste, "editor.paste", LocKeys::HierarchyPaste, LocKeys::MenuEdit,
                Bind(ImGuiKey_V, true), {}},
            // Shift 를 정확히 견주므로 Ctrl+Shift+V 가 위의 Ctrl+V 를 오발동시키지 않는다(기존과 같다).
            {EditorShortcut::PasteAsChild, "editor.paste_as_child", LocKeys::HierarchyPasteAsChild, LocKeys::MenuEdit,
                Bind(ImGuiKey_V, true, true), {}},
            {EditorShortcut::DeleteSelection, "editor.delete_selection", LocKeys::HierarchyDelete, LocKeys::MenuEdit,
                Bind(ImGuiKey_Delete), {}},
            {EditorShortcut::TogglePlay, "editor.toggle_play", LocKeys::MenuSimulationPlay, LocKeys::MenuSimulation,
                Bind(ImGuiKey_F5), {}},
            {EditorShortcut::TogglePause, "editor.toggle_pause", LocKeys::MenuSimulationPause, LocKeys::MenuSimulation,
                Bind(ImGuiKey_F6), {}},
        };
        static_assert(
            sizeof(Table) / sizeof(Table[0]) == static_cast<std::size_t>(EditorShortcut::Count),
            "every shortcut needs a row, or ActionId returns the wrong one");

        // 표의 한 줄을 관리자에 올리는 할 일. 판단은 아래 세 함수가 든다 - 메뉴와 키가 같은 것을 부른다.
        class BuiltinHandler final : public IEditorShortcutHandler
        {
        public:
            explicit BuiltinHandler(EditorShortcut id)
                : m_id(id)
            {
            }
            bool CanExecute(const EditorApplication& editor) const override
            {
                return EditorShortcuts::CanExecute(editor, m_id);
            }
            const char* WhyBlocked(const EditorApplication& editor) const override
            {
                return EditorShortcuts::WhyBlocked(editor, m_id);
            }
            bool Execute(EditorApplication& editor) override
            {
                return EditorShortcuts::Execute(editor, m_id);
            }

        private:
            EditorShortcut m_id;
        };
    }

    const char* ActionId(EditorShortcut id)
    {
        const std::size_t index = static_cast<std::size_t>(id);
        // 표는 열거 차례 그대로다(위의 static_assert 가 개수를 지킨다).
        return Table[index < static_cast<std::size_t>(EditorShortcut::Count) ? index : 0].actionId;
    }

    void RegisterBuiltins(EditorShortcutManager& shortcuts)
    {
        for (const BuiltinRow& row : Table)
        {
            EditorShortcutDesc desc;
            desc.id = row.actionId;
            desc.labelKey = row.labelKey;
            desc.categoryKey = row.categoryKey;
            desc.primary = row.primary;
            desc.secondary = row.secondary;
            // 저장은 예외다 - 글자를 치는 중에도 Ctrl+S 는 저장이어야 한다.
            desc.whileTyping = row.id == EditorShortcut::SaveCanvas;
            // **게임이 키를 받는 동안은 재생 제어만 남긴다**(D-214). 게임의 Delete 가 선택한 오브젝트를 지우고 Ctrl+Z 가
            // 편집을 되돌리면 안 된다. 기존 엔진은 둘 다 받게 두었다.
            desc.duringGame = row.id == EditorShortcut::TogglePlay || row.id == EditorShortcut::TogglePause;
            desc.handler = MakeOwnerPtr<BuiltinHandler>(row.id);
            shortcuts.Register(std::move(desc));
        }
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

    EditorShortcutText Describe(const EditorApplication& editor, EditorShortcut id)
    {
        return EditorShortcutManager::Describe(editor.GetShortcuts().Find(ActionId(id)).primary);
    }
}
