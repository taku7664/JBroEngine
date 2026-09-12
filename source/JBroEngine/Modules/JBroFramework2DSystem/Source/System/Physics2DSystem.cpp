#include <JBro/Framework2DSystem/System/Physics2DSystem.h>

#include "Physics2DGeometry.h"

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <limits>

namespace JBro::System
{
    int Physics2DSystem::GetExecutionOrder() const
    {
        return 200;
    }

    void Physics2DSystem::SetGravity(Vec2 gravity)
    {
        m_gravity = gravity;
    }

    Vec2 Physics2DSystem::GetGravity() const
    {
        return m_gravity;
    }

    bool Physics2DSystem::Raycast(
        Vec2 origin,
        Vec2 direction,
        float distance,
        Collision2D& hit) const
    {
        hit = {};
        if (m_canvas == nullptr)
        {
            return false;
        }

        Canvas& canvas = *m_canvas;
        const float directionLengthSquared =
            direction.x * direction.x + direction.y * direction.y;
        constexpr float DirectionEpsilonSquared = 0.000000000001f;
        if (distance < 0.0f
            || directionLengthSquared <= DirectionEpsilonSquared)
        {
            return false;
        }

        const float inverseLength = 1.0f / std::sqrt(directionLengthSquared);
        direction.x *= inverseLength;
        direction.y *= inverseLength;
        float closestDistance = std::numeric_limits<float>::max();
        canvas.ForEach<Component::Collider2D>(
            [&canvas, origin, direction, distance, &hit, &closestDistance](
                Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }

            Internal::ColliderGeometry geometry;
            if (false == Internal::CalculateColliderGeometry(canvas, collider, geometry))
            {
                return;
            }

            float candidateDistance = 0.0f;
            Vec2 candidateNormal;
            bool intersects = false;
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                intersects = Internal::RaycastBox(
                    origin,
                    direction,
                    distance,
                    geometry,
                    candidateDistance,
                    candidateNormal);
            }
            else if (collider.shape == Component::ColliderShape2D::Circle)
            {
                intersects = Internal::RaycastCircle(
                    origin,
                    direction,
                    distance,
                    geometry,
                    candidateDistance,
                    candidateNormal);
            }

            if (false == intersects || candidateDistance >= closestDistance)
            {
                return;
            }

            GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
            if (owner == nullptr)
            {
                return;
            }
            closestDistance = candidateDistance;
            hit.other = owner->GetScriptHandle();
            hit.bodyType = Internal::GetBodyType(canvas, owner);
            hit.point = {
                origin.x + direction.x * candidateDistance,
                origin.y + direction.y * candidateDistance};
            hit.normal = candidateNormal;
        });
        return hit.other.GetInstanceId() != InvalidInstanceId;
    }

    void Physics2DSystem::OverlapBox(
        const Rect& area,
        Array<GameObjectHandle>& results) const
    {
        results.Clear();
        if (m_canvas == nullptr)
        {
            return;
        }

        Canvas& canvas = *m_canvas;
        canvas.ForEach<Component::Collider2D>(
            [&canvas, &area, &results](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }

            Internal::ColliderGeometry geometry;
            if (false == Internal::CalculateColliderGeometry(canvas, collider, geometry))
            {
                return;
            }

            bool intersects = false;
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                intersects = Internal::IntersectsBox(area, geometry);
            }
            else if (collider.shape == Component::ColliderShape2D::Circle)
            {
                intersects = Internal::IntersectsCircle(area, geometry);
            }
            if (false == intersects)
            {
                return;
            }

            GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
            if (owner == nullptr)
            {
                return;
            }

            const InstanceId ownerId = owner->GetInstanceId();
            for (const GameObjectHandle& existing : results)
            {
                if (existing.GetInstanceId() == ownerId)
                {
                    return;
                }
            }
            results.Add(owner->GetScriptHandle());
        });
    }

    void Physics2DSystem::OnInitialize(Canvas& canvas)
    {
        m_canvas = &canvas;
    }

    void Physics2DSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        if (fixedDeltaTime <= 0.0f)
        {
            return;
        }

        canvas.ForEach<Component::Rigidbody2D>(
            [this, &canvas, fixedDeltaTime](Component::Rigidbody2D& body)
        {
            if (false == body.IsActiveComponent()
                || body.bodyType == Component::BodyType2D::Static)
            {
                return;
            }

            GameObject* owner = Internal::CanvasAccess::GetOwner(body);
            Component::Transform2D* transform =
                canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (transform == nullptr || false == transform->IsActiveComponent())
            {
                return;
            }

            if (body.bodyType == Component::BodyType2D::Dynamic)
            {
                if (body.mass <= 0.0f)
                {
                    return;
                }

                body.linearVelocity.x +=
                    m_gravity.x * body.gravityScale * fixedDeltaTime;
                body.linearVelocity.y +=
                    m_gravity.y * body.gravityScale * fixedDeltaTime;

                if (body.linearDamping > 0.0f)
                {
                    float dampingFactor = 1.0f - body.linearDamping * fixedDeltaTime;
                    if (dampingFactor < 0.0f)
                    {
                        dampingFactor = 0.0f;
                    }
                    body.linearVelocity.x *= dampingFactor;
                    body.linearVelocity.y *= dampingFactor;
                }
            }

            transform->position.x += body.linearVelocity.x * fixedDeltaTime;
            transform->position.y += body.linearVelocity.y * fixedDeltaTime;
            if (false == body.fixedRotation)
            {
                transform->rotation += body.angularVelocity * fixedDeltaTime;
            }

            Component::WorldTransform2D* world =
                canvas.FindComponentRaw<Component::WorldTransform2D>(owner);
            if (world != nullptr)
            {
                world->dirty = true;
            }
        });
    }

    void Physics2DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        m_canvas = nullptr;
    }
}
