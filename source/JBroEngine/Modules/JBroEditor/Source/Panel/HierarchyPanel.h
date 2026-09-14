#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Types/String.h>

namespace JBro
{
    class GameObject;

    // 캔버스에 있는 오브젝트를 부모-자식 그대로 보여 주고, 고르면 인스펙터가 받는다.
    //
    // 줄은 공용 트리 위젯이 그린다(ProjectRule §11.1). `ImGui::TreeNodeEx` 로는
    // 줄의 남은 자리를 알 수 없어 이름 옆에 무엇도 얹을 수 없다.
    class HierarchyPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Left; }

    private:
        // 찾는 글자에 걸리는가. 자식이 걸리면 부모도 남는다 - 그러지 않으면
        // 걸린 자식이 갈 곳을 잃는다.
        bool Matches(const GameObject& object) const;
        void DrawObject(GameObject& object);

        EditorApplication* m_editor = nullptr;
        String m_filter;
    };
}
