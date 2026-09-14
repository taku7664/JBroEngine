#include <JBro/Editor/Command/HierarchyCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        Component::Transform2D* FindTransform(Canvas& canvas, GameObject& object)
        {
            return canvas.FindComponentRaw<Component::Transform2D>(&object);
        }
    }

    MoveInHierarchyCommand::MoveInHierarchyCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        EditorObjectId newParentId,
        std::size_t siblingIndex)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_objectId(objectId)
    {
        GameObject* object = registry.Resolve(objectId);
        if (object == nullptr)
        {
            return;
        }
        GameObject* newParent = newParentId != InvalidEditorObjectId
            ? registry.Resolve(newParentId)
            : nullptr;

        // **자기 밑으로는 못 들어간다.** `SetParent` 가 거절하지만, 거절당한
        // 뒤에 순서만 바뀌어 있으면 반쯤 적용된 상태가 된다 - 아예 뜨지 않는다.
        for (GameObject* walk = newParent; walk != nullptr; walk = walk->GetParent())
        {
            if (walk == object)
            {
                return;
            }
        }

        if (false == Capture(m_before))
        {
            return;
        }
        m_after.parentId = newParentId;
        m_after.siblingIndex = siblingIndex;
        if (false == ComputeWorldStay(*object, newParent, m_after))
        {
            return;
        }
        m_captured = true;
    }

    const char* MoveInHierarchyCommand::GetName() const
    {
        return "Move In Hierarchy";
    }

    bool MoveInHierarchyCommand::Capture(Placement& placement) const
    {
        GameObject* object = m_registry->Resolve(m_objectId);
        if (object == nullptr)
        {
            return false;
        }
        GameObject* parent = object->GetParent();
        placement.parentId = parent != nullptr ? m_registry->Track(parent)
            : InvalidEditorObjectId;
        placement.siblingIndex = 0;
        if (parent != nullptr)
        {
            parent->FindChildIndex(object, placement.siblingIndex);
        }
        if (Component::Transform2D* transform = FindTransform(*m_canvas, *object))
        {
            placement.hasTransform = true;
            placement.position = transform->position;
            placement.rotation = transform->rotation;
            placement.scale = transform->scale;
        }
        return true;
    }

    bool MoveInHierarchyCommand::ComputeWorldStay(
        GameObject& object, GameObject* newParent, Placement& placement) const
    {
        Component::Transform2D* transform = FindTransform(*m_canvas, object);
        if (transform == nullptr)
        {
            // 트랜스폼이 없으면 지킬 자리도 없다. 부모와 순서만 바뀐다.
            placement.hasTransform = false;
            return true;
        }
        placement.hasTransform = true;

        if (false == transform->worldValid)
        {
            // **월드 값이 아직 안 서 있다.** 한 프레임도 돌지 않았거나 꺼져 있는
            // 오브젝트다 - 짐작해서 쓰면 엉뚱한 자리로 간다. 로컬을 그대로 둔다.
            placement.position = transform->position;
            placement.rotation = transform->rotation;
            placement.scale = transform->scale;
            return true;
        }

        Component::Transform2D* parentTransform = newParent != nullptr
            ? FindTransform(*m_canvas, *newParent)
            : nullptr;
        if (parentTransform == nullptr || false == parentTransform->worldValid)
        {
            // 새 부모에 트랜스폼이 없으면 그 밑의 로컬이 곧 월드다.
            placement.position = transform->worldPosition;
            placement.rotation = transform->worldRotation;
            placement.scale = transform->worldScale;
            return true;
        }

        // 부모의 월드를 되돌린다. 우리 월드는 이동·회전·크기로 분해되어 있어
        // 행렬을 뒤집지 않아도 된다.
        const Vec2 offset{
            transform->worldPosition.x - parentTransform->worldPosition.x,
            transform->worldPosition.y - parentTransform->worldPosition.y};
        const float angle = -parentTransform->worldRotation;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const Vec2 rotated{
            offset.x * cosine - offset.y * sine,
            offset.x * sine + offset.y * cosine};

        // **0 으로 나누지 않는다.** 크기가 0 인 부모 밑에서는 월드 자리를 지킬
        // 방법이 없다(그 부모 아래의 모든 점이 한 점이다). 로컬을 그대로 둔다.
        const Vec2 parentScale = parentTransform->worldScale;
        if (std::fabs(parentScale.x) < 0.000001f || std::fabs(parentScale.y) < 0.000001f)
        {
            placement.position = transform->position;
            placement.rotation = transform->rotation;
            placement.scale = transform->scale;
            return true;
        }

        placement.position = {rotated.x / parentScale.x, rotated.y / parentScale.y};
        placement.rotation = transform->worldRotation - parentTransform->worldRotation;
        placement.scale = {
            transform->worldScale.x / parentScale.x,
            transform->worldScale.y / parentScale.y};
        return true;
    }

    bool MoveInHierarchyCommand::Apply(const Placement& placement)
    {
        GameObject* object = m_registry->Resolve(m_objectId);
        if (object == nullptr)
        {
            return false;
        }
        GameObject* parent = placement.parentId != InvalidEditorObjectId
            ? m_registry->Resolve(placement.parentId)
            : nullptr;

        object->SetParent(parent);
        if (parent != nullptr)
        {
            // **부모를 정한 뒤에 자리를 정한다.** `SetParent` 는 맨 뒤에 붙인다.
            parent->SetChildIndex(object, placement.siblingIndex);
        }
        if (placement.hasTransform)
        {
            if (Component::Transform2D* transform = FindTransform(*m_canvas, *object))
            {
                transform->position = placement.position;
                transform->rotation = placement.rotation;
                transform->scale = placement.scale;
                // 다음 프레임에 월드를 다시 세우게 한다.
                transform->worldValid = false;
            }
        }
        return true;
    }

    bool MoveInHierarchyCommand::Execute()
    {
        if (false == m_captured)
        {
            return false;
        }
        // 제자리로 옮기는 것은 편집이 아니다. 스택에 올리면 Ctrl+Z 가 헛걸음한다.
        if (m_before.parentId == m_after.parentId
            && m_before.siblingIndex == m_after.siblingIndex)
        {
            return false;
        }
        return Apply(m_after);
    }

    void MoveInHierarchyCommand::Undo()
    {
        Apply(m_before);
    }

    void MoveInHierarchyCommand::Redo()
    {
        Apply(m_after);
    }
}
