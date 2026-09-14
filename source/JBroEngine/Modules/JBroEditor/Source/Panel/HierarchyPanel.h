#pragma once

#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    class GameObject;

    // 캔버스에 있는 오브젝트를 부모-자식 그대로 보여 주고, 고르면 인스펙터가 받는다.
    class HierarchyPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Left; }

    private:
        void DrawObject(GameObject& object);

        EditorApplication* m_editor = nullptr;
    };
}
