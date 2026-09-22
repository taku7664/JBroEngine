#pragma once

#include <JBro/Core/Core.h>

#include <imgui.h>

namespace JBro
{
    class EditorApplication;

    // 에디터의 단축키다(D-132). 기존 엔진 `CEditorShortcutManager` 자리다.
    //
    // **키와 그 뜻을 한 표에 둔다.** 그러지 않으면 누르는 자리(`ImGui::Shortcut`)와
    // 보이는 자리(메뉴의 `"Ctrl+Z"` 글자)와 할 수 있는지 재는 자리가 셋으로 갈린다 —
    // 실제로 갈려 있었고, 메뉴에 적힌 글자는 코드에 박힌 문자열이었다.
    enum class EditorShortcut : std::uint8_t
    {
        SaveCanvas,
        Undo,
        Redo,
        Copy,
        Paste,
        PasteAsChild,
        DeleteSelection,
        TogglePlay,
        TogglePause,
        Count
    };

    // 키 하나와 그에 붙는 조합키다. `key` 가 `ImGuiKey_None` 이면 비어 있는 자리다.
    struct EditorShortcutBinding
    {
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
        bool shift = false;

        bool IsSet() const
        {
            return key != ImGuiKey_None;
        }
    };

    struct EditorShortcutInfo
    {
        EditorShortcut id = EditorShortcut::Count;
        // 보이는 이름과 그것이 속한 무리다. 둘 다 로컬라이징 키다(§11.2).
        const char* labelKey = nullptr;
        const char* categoryKey = nullptr;
        EditorShortcutBinding primary;
        // 두 번째 조합이다. 다시 실행이 `Ctrl+Y` 와 `Ctrl+Shift+Z` 둘 다인 것이 그 예다.
        EditorShortcutBinding secondary;
    };

    // 한 줄에 적을 수 있는 가장 긴 글자(`Ctrl+Shift+Delete`)보다 넉넉하다.
    struct EditorShortcutText
    {
        char value[48] = {};
    };

    namespace EditorShortcuts
    {
        JArrayView<EditorShortcutInfo> All();
        const EditorShortcutInfo& Find(EditorShortcut id);
        // 지금 할 수 있는가. **메뉴가 회색으로 그릴지 정하는 값**이고, 누른 키도 이것을 본다 -
        // 둘이 갈리면 메뉴에서는 못 하는데 키로는 되는 자리가 생긴다.
        bool CanExecute(const EditorApplication& editor, EditorShortcut id);
        bool Execute(EditorApplication& editor, EditorShortcut id);
        // `Ctrl+Shift+Z` 같은 글자. 비어 있는 자리는 빈 글자다.
        EditorShortcutText Describe(const EditorShortcutBinding& binding);
        // 그 손짓의 **첫 번째** 조합을 글자로. 메뉴 항목의 오른쪽에 적는 값이다.
        EditorShortcutText Describe(EditorShortcut id);
        // 매 프레임 한 번. 눌린 것이 있으면 그 일을 한다.
        // **글자 칸에 타자를 치는 중이면 건너뛴다** - 이름을 고치다 Ctrl+Z 를 누르면
        // 글자를 되돌려야지 씬을 되돌리면 안 된다.
        void ProcessInput(EditorApplication& editor);
    }
}
