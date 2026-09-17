#include <JBro/Framework3DSystem/System/Transform3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::System
{
    namespace
    {
        void PropagateWorldTransform(
            Canvas& canvas,
            GameObject& object,
            Component::Transform3D& transform,
            const Vec3& parentPosition,
            const Quaternion& parentRotation,
            const Vec3& parentScale)
        {
            // 분해된 값으로 겹친다(framework3d-plan §2.1). 자식의 위치는 부모의 스케일·회전을 거쳐
            // 부모 위치에 더해진다. 비균등 스케일 아래의 회전이 만드는 전단은 잃는다.
            transform.worldPosition = Add(parentPosition,
                Rotate(parentRotation, Multiply(transform.position, parentScale)));
            transform.worldRotation = Normalize(Multiply(parentRotation, transform.rotation));
            transform.worldScale = Multiply(parentScale, transform.scale);
            transform.worldValid = true;

            for (const SafePtr<GameObject>& childReference : object.GetChildren())
            {
                GameObject* child = childReference.TryGet();
                if (child == nullptr)
                {
                    continue;
                }
                Component::Transform3D* childTransform =
                    canvas.FindComponentRaw<Component::Transform3D>(child);
                if (childTransform == nullptr || false == childTransform->IsActiveComponent())
                {
                    continue;
                }
                PropagateWorldTransform(canvas, *child, *childTransform,
                    transform.worldPosition, transform.worldRotation, transform.worldScale);
            }
        }
    }

    int Transform3DSystem::GetExecutionOrder() const
    {
        return 100;
    }

    void Transform3DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        canvas.ForEach<Component::Transform3D>([](Component::Transform3D& transform)
        {
            transform.worldValid = false;
        });
        canvas.ForEach<Component::Transform3D>([&canvas](Component::Transform3D& transform)
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
            // 부모에 `Transform3D` 가 있으면 뿌리가 아니다. 부모가 내려오며 채운다. 부모의 것이 꺼져
            // 있어도 뿌리로 보지 않는다 - 그러면 자식이 원점으로 튄다(A4 와 같은 함정).
            if (canvas.FindComponentRaw<Component::Transform3D>(owner->GetParent()) != nullptr)
            {
                return;
            }
            PropagateWorldTransform(canvas, *owner, transform, {}, {}, {1.0f, 1.0f, 1.0f});
        });
    }
}
