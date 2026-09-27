#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Editor/EditorShortcutManager.h>

namespace JBro
{
    class EditorApplication;

    // 에디터의 **전역** 단축키 열이다(D-132). 한 프레임 진행은 D-242 에서 더했다. 누르는 자리·보이는 글자·바꾸는 자리는 `EditorShortcutManager` 한 표이고(D-228),
    // 여기는 그 표에 올리는 기본 조합과 할 일(할 수 있는지·왜 못 하는지·하기)을 든다. 메뉴는 이 열거로 가리킨다.
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
        StepFrame,
        Count
    };

    namespace EditorShortcuts
    {
        // 표에 저장되는 이름(`editor.save_canvas` 같은 것). 설정 파일에 이 이름으로 사용자의 키가 적힌다.
        const char* ActionId(EditorShortcut id);
        // 에디터 전역 단축키 열을 기본 조합(기존 엔진과 같다)으로 관리자에 등록한다. 에디터가 켤 때 한 번.
        void RegisterBuiltins(EditorShortcutManager& shortcuts);
        // 지금 할 수 있는가. **메뉴가 회색으로 그릴지 정하는 값**이고, 누른 키도 이것을 본다 -
        // 둘이 갈리면 메뉴에서는 못 하는데 키로는 되는 자리가 생긴다.
        bool CanExecute(const EditorApplication& editor, EditorShortcut id);
        // **왜 지금 못 하는가**(D-181, 기존 `EditorSimulationGuard::GetSaveBlockedMessage`).
        // 할 수 있으면 nullptr 이고, 막혀 있으면 번역된 한 줄이다. 메뉴가 회색 항목에
        // 띄운다 - 회색으로만 두면 무엇을 해야 켜지는지 알 수 없다.
        //
        // `CanExecute` 옆에 두는 이유: 막는 조건이 그쪽에 있으므로, 떨어뜨려 놓으면
        // 조건을 고칠 때 한쪽만 고쳐져 엉뚱한 까닭이 뜬다.
        const char* WhyBlocked(const EditorApplication& editor, EditorShortcut id);
        bool Execute(EditorApplication& editor, EditorShortcut id);
        // 그 손짓의 **지금** 첫 번째 조합을 글자로(사용자가 바꿨으면 바꾼 것). 메뉴 항목의 오른쪽에 적는 값이다.
        EditorShortcutText Describe(const EditorApplication& editor, EditorShortcut id);
    }
}
