#pragma once

#include <JBro/Editor/EditorPopup.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    struct EditorActionInfo;

    // **명령 팔레트**다(todo "에디터 공용 기반" 9 번, D-285). 행동 표(D-284)의 행동을 이름·무리·저장 이름·조합키 글자로 찾아 실행한다.
    // 행동 표가 곧 목록이라 손으로 적은 명령이 없다. 지금 할 수 없는 행동도 빼지 않고 회색으로 두고 까닭을 말한다(D-180·D-181).
    // Enter 는 보이는 첫 항목이 아니라 **할 수 있는 첫 항목**을 한다. Esc 로 닫는다.
    class CommandPalettePopup final : public EditorPopup
    {
    public:
        static constexpr const char* PopupId = "editor.command_palette";

        const char* GetTitle() const override;
        const char* GetId() const override;
        float GetInitialWidth() const override;
        float GetInitialHeight() const override;
        void OnDraw(EditorApplication& editor) override;

        // 팔레트에 서는 행동을 `query` 로 걸러 표의 차례로 `out` 에 담는다. 컴포넌트 하나를 두고만 뜻이 있는 행동(컴포넌트 메뉴의 것)과
        // 팔레트 자신은 서지 않는다. 빈 검색어는 모두 맞는다.
        static void Collect(const EditorApplication& editor, const char* query, Array<const EditorActionInfo*>& out);

    private:
        String m_search;
        Array<const EditorActionInfo*> m_matches;
        bool m_focusSearch = true;
    };

    namespace CommandPalette
    {
        // 팔레트를 여는 행동을 표에 올린다. 행동 표가 부른다.
        void RegisterAction();
    }
}
