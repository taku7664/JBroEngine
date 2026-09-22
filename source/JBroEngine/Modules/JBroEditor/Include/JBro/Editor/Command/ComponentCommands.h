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

        // 값을 들고 붙인다(D-167, 컴포넌트 붙여넣기). 붙인 직후 떠 둔 값을 써 넣으므로
        // 되돌리기는 그냥 떼는 것이고, 다시 하기는 값까지 돌아온다.
        AddComponentCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            NameId typeName,
            const ComponentSnapshot& values);

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
        // 붙이면서 써 넣을 값이다. 비어 있으면 맨 컴포넌트를 붙인다.
        ComponentSnapshot m_values;
        bool m_hasValues = false;
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
        // 오브젝트의 컴포넌트 슬롯 중 몇 번째였는가(타입을 가리지 않는다).
        // 되돌릴 때 그 자리로 보낸다 - 맨 뒤에 두면 같은 타입끼리 차례가 바뀌어
        // 앞서 쌓인 커맨드의 "몇 번째" 가 다른 컴포넌트를 가리킨다.
        std::size_t m_slotIndex = 0;
        // 값을 다 떴는가. 못 떴으면 떼지 않는다.
        bool m_captured = false;
    };

    // 이미 붙어 있는 컴포넌트에 떠 둔 값을 덮어쓴다(D-167, `컴포넌트 값 붙여넣기`).
    //
    // 기존 엔진에는 붙이는 쪽(새로 하나 더 붙이기)만 있었다. `Transform2D` 처럼 하나만 있어야
    // 뜻이 서는 타입에서는 그것이 쓸 수 있는 손짓이 아니다 - 같은 타입을 하나 더 얹으면
    // 어느 쪽이 쓰이는지 화면에서 알 수 없다.
    //
    // **덮기 전의 값을 못 뜨면 덮지 않는다**(D-76 과 같은 규칙).
    class PasteComponentValuesCommand final : public EditorCommand
    {
    public:
        PasteComponentValuesCommand(
            EditorObjectRegistry& registry,
            const ComponentAddress& address,
            const ComponentSnapshot& values);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        bool Apply(const ComponentSnapshot& values);

        EditorObjectRegistry* m_registry = nullptr;
        ComponentAddress m_address;
        ComponentSnapshot m_values;
        ComponentSnapshot m_before;
        bool m_captured = false;
    };

    // 컴포넌트 슬롯 하나를 다른 자리로 옮긴다(기존 엔진 `CReorderComponentCommand`). 되돌리면
    // 도로 옮긴다. **스크립트 실행 순서가 이 자리를 따른다**(D-45, A3) - 순서를 바꾸는 손짓이
    // 되돌릴 수 없으면 실행 순서를 되돌릴 수 없다.
    //
    // 슬롯 번호로 가리킨다. 되돌리기는 차례대로만 오므로 그때의 오브젝트는 이 커맨드를 실행한
    // 직후와 같고, `to` 자리에 있는 것이 옮긴 그것이다. 같은 자리나 끝을 넘는 번호는 거절한다.
    class MoveComponentCommand final : public EditorCommand
    {
    public:
        MoveComponentCommand(
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            std::size_t fromSlot,
            std::size_t toSlot);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        bool Move(std::size_t from, std::size_t to);

        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        std::size_t m_from = 0;
        std::size_t m_to = 0;
    };
}
