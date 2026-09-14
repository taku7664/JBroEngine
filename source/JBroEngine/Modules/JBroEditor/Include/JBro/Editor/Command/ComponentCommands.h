#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/ComponentSnapshot.h>

#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/NameTable.h>

namespace JBro
{
    class Canvas;
    class GameObject;

    // 컴포넌트 하나를 붙인다. 되돌리면 뗀다.
    //
    // 붙는 자리는 늘 맨 끝이라 되돌릴 때 찾을 자리도 맨 끝이다.
    class AddComponentCommand final : public EditorCommand
    {
    public:
        AddComponentCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            NameId typeName);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 붙인 컴포넌트다. 부른 쪽이 인스펙터를 그리로 옮기는 데 쓴다.
        ComponentBase* GetComponent() const;

    private:
        bool Attach();

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        ComponentAddress m_address;
        NameId m_typeName = InvalidNameId;
        bool m_added = false;
    };

    // 컴포넌트 하나를 뗀다. 되돌리면 다시 붙이고 값을 도로 써 넣는다.
    //
    // **떼기 전에 값을 떠 두지 못하면 떼지 않는다**(D-76 과 같은 규칙).
    // 되살릴 수 없는 것을 성공했다고 말하며 지우는 것이 가장 나쁘다.
    class RemoveComponentCommand final : public EditorCommand
    {
    public:
        RemoveComponentCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            ComponentBase* component);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        bool Detach();

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        ComponentAddress m_address;
        NameId m_typeName = InvalidNameId;
        ComponentSnapshot m_snapshot;
        // 값을 다 떴는가. 못 떴으면 떼지 않는다.
        bool m_captured = false;
    };
}
