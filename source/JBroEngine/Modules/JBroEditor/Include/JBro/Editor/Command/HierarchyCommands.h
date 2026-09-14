#pragma once

#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>

#include <JBro/Framework2D/Math2D.h>

namespace JBro
{
    class Canvas;
    class GameObject;

    // 계층에서 오브젝트를 옮긴다. 부모를 바꾸거나, 형제들 사이에서 자리를 바꾼다.
    //
    // 기존 엔진 `CMoveGameObjectInHierarchyCommand` 와 같은 자리다 - 부모·순서가
    // 드롭 한 번에 함께 바뀌므로 커맨드를 나누면 되돌리기가 쪼개진다.
    //
    // **월드 자리를 지킨다**(기존의 WorldStay). 부모가 바뀌면 같은 로컬 값이 다른
    // 월드 자리를 뜻하게 되므로, 끌어다 놓은 것이 화면에서 튄다. 새 부모 기준으로
    // 로컬을 다시 구해 그 자리에 머물게 한다.
    //
    // 우리 트랜스폼은 월드를 **분해해서** 들고 있어(`worldPosition`/`worldRotation`/
    // `worldScale`, D-47) 역행렬이 필요 없다. 기존은 행렬을 뒤집고 분해했다.
    class MoveInHierarchyCommand final : public EditorCommand
    {
    public:
        // `newParentId` 가 `InvalidEditorObjectId` 면 뿌리로 올린다.
        // `siblingIndex` 는 새 부모의 자식들 사이에서의 자리다. 끝을 넘으면 맨 뒤.
        MoveInHierarchyCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            EditorObjectId newParentId,
            std::size_t siblingIndex);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        // 한 오브젝트의 자리다. 부모·형제 번호·로컬 트랜스폼이 함께 움직인다.
        struct Placement
        {
            EditorObjectId parentId = InvalidEditorObjectId;
            std::size_t siblingIndex = 0;
            bool hasTransform = false;
            Vec2 position{0.0f, 0.0f};
            float rotation = 0.0f;
            Vec2 scale{1.0f, 1.0f};
        };

        bool Apply(const Placement& placement);
        // 지금 자리를 뜬다.
        bool Capture(Placement& placement) const;
        // 새 부모 아래에서 지금 월드 자리를 지키는 로컬 값을 구한다.
        bool ComputeWorldStay(GameObject& object, GameObject* newParent,
            Placement& placement) const;

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        Placement m_before;
        Placement m_after;
        // 뜨지 못했으면 옮기지 않는다 - 되돌릴 수 없는 것은 하지 않는다(D-76).
        bool m_captured = false;
    };
}
