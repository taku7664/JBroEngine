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

    // 부모를 가진 노드는 그 부모의 순회에서 처리된다. 여기서는 루트만 골라 내려간다.
    void Transform2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;

        // **먼저 전부 무효로 내린다.** 갱신되는 것은 아래에서 다시 참이 되고, 갱신되지
        // 않는 것(꺼진 것, 꺼진 부모 아래에 있는 것)은 무효로 남는다.
        //
        // 내리지 않으면 갱신이 멈춘 캐시가 "쓸 수 있다" 고 표시된 채로 남는다. 계층 끌어
        // 옮기기가 그 값으로 자리를 보존하므로(`HierarchyCommands`, "꺼져 있는 오브젝트다"
        // 라고 적어 둔 그 분기), 꺼 둔 사이에 부모가 움직였으면 철 지난 월드로 자리를 잡는다.
        canvas.ForEach<Component::Transform2D>([](Component::Transform2D& transform)
        {
            transform.worldValid = false;
        });

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

            // **부모에 Transform 이 있으면 루트가 아니다.** 그 Transform 이 꺼져 있어도
            // 마찬가지다(A4) - 그러면 이 노드는 아무 루트에서도 닿지 않아 무효로 남고,
            // 그리기와 카메라가 서브트리를 통째로 건너뛴다.
            //
            // 예전에는 꺼진 부모를 루트 없음으로 보아 **단위행렬에서** 전파했다. 부모
            // 오브젝트는 켜 둔 채 Transform 만 꺼면 자식 서브트리가 조용히 원점으로 튀었다.
            // 부모에 Transform 이 아예 없는 경우는 다르다 - 물려받을 자리가 없으므로
            // 그 아래는 자기 로컬이 곧 월드다.
            if (canvas.FindComponentRaw<Component::Transform2D>(owner->GetParent()) != nullptr)
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
