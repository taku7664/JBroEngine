#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/EditorShortcutManager.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // **에디터 설정**(설정 메뉴, D-230). 프로젝트가 아니라 **이 사람의 에디터**에 대한 것이다 - 사용자별 환경설정 파일(D-228)에 남는다.
    //
    // 왼쪽이 항목 목록이고 오른쪽이 고른 항목의 화면이다. 지금 항목은 **단축키** 하나다: 등록된 단축키를 무리별로 늘어놓고, 조합 칸을
    // 누르면 다음에 누른 키가 그 조합이 된다(Esc 는 취소). 검색은 이름·무리·저장 이름·지금 조합 글자를 본다. 겹치는 조합은 줄 끝에
    // 표시하고 까닭을 툴팁으로 말한다.
    //
    // 처음에는 닫혀 있다. 설정 메뉴에서 연다.
    class EditorSettingsPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDestroy() override;
        void OnUpdate(float deltaTime) override;
        void OnDraw() override;

    private:
        enum class Page : std::uint8_t
        {
            Shortcuts,
        };

        void DrawShortcuts();
        void DrawShortcutRow(std::uint32_t index, const EditorShortcutView& view);
        // 잡는 중이면 이번 프레임에 눌린 키를 본다. Esc 는 취소, 다른 키는 그 조합이 된다.
        void UpdateCapture();
        void StartCapture(const char* id, std::uint32_t slot);
        void StopCapture();
        static void AnswerResetAll(EditorApplication& editor, int choice, void* user);

        EditorApplication* m_editor = nullptr;
        Page m_page = Page::Shortcuts;
        String m_search;
        // 잡고 있는 단축키의 저장 이름과 자리. 이름이 비면 잡고 있지 않다 - 번호가 아니라 이름으로 든다(등록이 풀려 번호가 밀려도 맞다).
        String m_captureId;
        std::uint32_t m_captureSlot = 0;
        // 한 프레임에 한 번 채운다. 늘 같은 배열을 다시 쓴다.
        Array<ShortcutConflict> m_conflicts;
    };
}
