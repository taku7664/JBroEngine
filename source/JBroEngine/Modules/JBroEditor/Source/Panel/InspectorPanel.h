#pragma once

#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    struct PropertyEditInfo;
    struct PropertyTable;
    struct TypeDescriptor;

    // 고른 오브젝트의 컴포넌트와 그 필드를 보여 주고 고치게 한다.
    //
    // **컴포넌트 타입을 하나도 모른다.** 리플렉션(D-56)이 내주는 필드를 타고 내려가
    // 잎사귀에서 코덱을 만난다. 새 컴포넌트를 더해도 이 파일은 그대로다 -
    // 기존 엔진에서 "이 타입은 이렇게 그린다" 는 지식이 여섯 군데 흩어졌던 자리다.
    class InspectorPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }

    private:
        // 값 하나를 그린다. 구조를 가진 타입이면 필드를 타고 내려간다.
        void DrawValue(
            const char* label,
            const TypeDescriptor& type,
            void* address,
            const PropertyEditInfo* edit);
        void DrawFields(const PropertyTable& table, void* owner);

        EditorApplication* m_editor = nullptr;
    };
}
