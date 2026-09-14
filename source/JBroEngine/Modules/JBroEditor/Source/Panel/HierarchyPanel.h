#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Types/SafePtr.h>
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
        // 끌고 있는 것을 받는 자리. 줄 위는 "그 밑으로", 줄 사이는 "그 자리에".
        void DrawDropTarget(GameObject* parent, std::size_t siblingIndex);
        void DrawDragSource(GameObject& object);
        // 이번 프레임에 떨어진 것을 실제로 옮긴다.
        void FlushPendingMove();

        EditorApplication* m_editor = nullptr;
        String m_filter;

        // **프레임이 끝난 뒤에 옮긴다.** 그리는 도중에 부모를 바꾸면 지금 돌고
        // 있는 자식 배열이 그 자리에서 달라진다 - 순회가 죽은 자리를 읽는다.
        SafePtr<GameObject> m_dragged;
        SafePtr<GameObject> m_dropParent;
        std::size_t m_dropIndex = 0;
        bool m_dropToRoot = false;
        bool m_hasDrop = false;
    };
}
