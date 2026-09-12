#include <JBro/Framework2DSystem/System/Transform2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::System
{
    namespace
    {
        void StoreWorldTransform(
            Component::WorldTransform2D& world,
            const Matrix3x2& matrix,
            float rotation,
            const Vec2& scale)
        {
            world.matrix = matrix;
            world.position = {matrix.m31, matrix.m32};
            world.rotation = rotation;
            world.scale = scale;
            world.dirty = false;
        }

        void PropagateWorldTransform(
            Canvas& canvas,
            GameObject& object,
            Component::Transform2D& local,
            const Matrix3x2& parentWorld,
            float parentRotation,
            const Vec2& parentScale)
        {
            if (false == local.IsActiveComponent())
            {
                return;
            }

            const Matrix3x2 localMatrix = MakeTransformMatrix2D(
                local.position,
                local.rotation,
                local.scale);
            const Matrix3x2 worldMatrix = MultiplyMatrix3x2(localMatrix, parentWorld);
            const float worldRotation = local.rotation + parentRotation;
            const Vec2 worldScale = {
                local.scale.x * parentScale.x,
                local.scale.y * parentScale.y};

            Component::WorldTransform2D* world =
                canvas.GetComponent<Component::WorldTransform2D>(&object);
            if (world != nullptr && world->IsActiveComponent())
            {
                StoreWorldTransform(*world, worldMatrix, worldRotation, worldScale);
            }

            for (const SafePtr<GameObject>& childReference : object.GetChildren())
            {
                GameObject* child = childReference.TryGet();
                if (child == nullptr)
                {
                    continue;
                }

                Component::Transform2D* childLocal =
                    canvas.GetComponent<Component::Transform2D>(child);
                if (childLocal == nullptr || false == childLocal->IsActiveComponent())
                {
                    continue;
                }
                PropagateWorldTransform(
                    canvas,
                    *child,
                    *childLocal,
                    worldMatrix,
                    worldRotation,
                    worldScale);
            }
        }
    }

    int Transform2DSystem::GetExecutionOrder() const
    {
        return 100;
    }

    void Transform2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        canvas.ForEach<Component::Transform2D>([&canvas](Component::Transform2D& local)
        {
            if (false == local.IsActiveComponent())
            {
                return;
            }

            GameObject* owner = local.GetOwner();
            if (owner == nullptr)
            {
                return;
            }

            GameObject* parent = owner->GetParent();
            Component::Transform2D* parentLocal =
                canvas.GetComponent<Component::Transform2D>(parent);
            if (parentLocal != nullptr && parentLocal->IsActiveComponent())
            {
                return;
            }

            PropagateWorldTransform(
                canvas,
                *owner,
                local,
                {},
                0.0f,
                {1.0f, 1.0f});
        });
    }
}
