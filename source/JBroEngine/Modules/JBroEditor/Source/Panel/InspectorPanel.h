#pragma once

#include <JBro/Editor/Command/SetPropertyCommand.h>

#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    class ComponentBase;
    struct PropertyEditInfo;
    struct PropertyTable;
    struct TypeDescriptor;

    // 고른 오브젝트의 컴포넌트와 그 필드를 보여 주고 고치게 한다.
    //
    // **컴포넌트 타입을 하나도 모른다.** 리플렉션(D-56)이 내주는 필드를 타고 내려가
    // 잎사귀에서 코덱을 만난다. 새 컴포넌트를 더해도 이 파일은 그대로다 -
    // 기존 엔진에서 "이 타입은 이렇게 그린다" 는 지식이 여섯 군데 흩어졌던 자리다.
    //
    // **값을 직접 쓰지 않는다.** 위젯이 바꾼 값을 도로 되돌려 놓고 커맨드를 만들어
    // 매니저에 넣는다 - 그래야 Ctrl+Z 가 그 편집을 안다(D-71).
    class InspectorPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }

    private:
        // 지금 그리는 컴포넌트와, 거기서 여기까지 내려온 길이다. 잎사귀에서
        // 커맨드를 만들 때 둘 다 필요하다.
        struct Context
        {
            ComponentBase* component = nullptr;
            ComponentTypeId typeId = 0;
            SetPropertyCommand::Path path;
        };

        // 값 하나를 그린다. 구조를 가진 타입이면 필드를 타고 내려간다.
        void DrawValue(
            const char* label,
            const TypeDescriptor& type,
            void* address,
            const PropertyEditInfo* edit,
            Context& context);
        void DrawFields(const PropertyTable& table, void* owner, Context& context);
        // 위젯이 값을 바꿨다. 되돌려 놓고 커맨드로 다시 적용한다.
        void CommitEdit(
            const TypeDescriptor& type,
            void* address,
            const String& before,
            Context& context);

        EditorApplication* m_editor = nullptr;
    };
}
