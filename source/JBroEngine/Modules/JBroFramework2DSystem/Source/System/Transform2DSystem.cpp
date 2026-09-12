#include <JBro/Framework2DSystem/System/Transform2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::System
{
    namespace
    {
        // 로컬과 월드가 한 컴포넌트에 있으므로(D-47) 노드마다 조회는 자식 Transform 하나뿐이다.
        // 예전에는 같은 노드에서 WorldTransform2D 를 한 번 더 찾아야 했다.
        void PropagateWorldTransform(
            Canvas& canvas,
            GameObject& object,
            Component::Transform2D& transform,
            const Matrix3x2& parentWorld,
            float parentRotation,
            const Vec2& parentScale)
        {
            const Matrix3x2 localMatrix = MakeTransformMatrix2D(
                transform.position,
                transform.rotation,
                transform.scale);

            transform.world = MultiplyMatrix3x2(localMatrix, parentWorld);
            transform.worldPosition = {transform.world.m31, transform.world.m32};
            transform.worldRotation = transform.rotation + parentRotation;
            transform.worldScale = {
                transform.scale.x * parentScale.x,
                transform.scale.y * parentScale.y};
            transform.worldValid = true;

            for (const SafePtr<GameObject>& childReference : object.GetChildren())
            {
                GameObject* child = childReference.TryGet();
                if (child == nullptr)
                {
                    continue;
                }

                Component::Transform2D* childTransform =
                    canvas.FindComponentRaw<Component::Transform2D>(child);
                if (childTransform == nullptr || false == childTransform->IsActiveComponent())
                {
                    continue;
                }
                PropagateWorldTransform(
                    canvas,
                    *child,
                    *childTransform,
                    transform.world,
                    transform.worldRotation,
                    transform.worldScale);
            }
        }
    }

    int Transform2DSystem::GetExecutionOrder() const
    {
        return 100;
    }

    // 활성 부모를 가진 노드는 그 부모의 순회에서 처리된다. 여기서는 루트만 골라 내려간다.
    void Transform2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        canvas.ForEach<Component::Transform2D>([&canvas](Component::Transform2D& transform)
        {
            if (false == transform.IsActiveComponent())
            {
                return;
            }

            GameObject* owner = Internal::CanvasAccess::GetOwner(transform);
            if (owner == nullptr)
            {
                return;
            }

            Component::Transform2D* parentTransform =
                canvas.FindComponentRaw<Component::Transform2D>(owner->GetParent());
            if (parentTransform != nullptr && parentTransform->IsActiveComponent())
            {
                return;
            }

            PropagateWorldTransform(
                canvas,
                *owner,
                transform,
                {},
                0.0f,
                {1.0f, 1.0f});
        });
    }
}
