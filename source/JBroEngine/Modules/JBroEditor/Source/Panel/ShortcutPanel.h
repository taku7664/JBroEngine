#pragma once

#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    // 단축키를 무리별로 늘어놓는다(D-132). 기존 엔진 `CShortcutReferenceTool` 자리다.
    //
    // **표 하나에서 그린다.** 손으로 적은 목록을 따로 두면 키를 바꿨을 때 이 창만
    // 옛 글자로 남는다 - 그러면 없느니만 못하다.
    //
    // 처음에는 닫혀 있다. 찾아 열어 보는 창이지 늘 보는 창이 아니다.
    class ShortcutPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        EditorApplication* m_editor = nullptr;
    };
}
