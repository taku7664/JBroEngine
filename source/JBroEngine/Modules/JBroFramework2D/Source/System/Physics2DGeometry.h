#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>

namespace JBro
{
    class Canvas;
    class GameObject;
}

namespace JBro::Internal
{
    struct ColliderGeometry
    {
        Vec2 center;
        Vec2 halfExtents;
        float radius = 0.0f;
    };

    bool CalculateColliderGeometry(
        Canvas& canvas,
        Component::Collider2D& collider,
        ColliderGeometry& result);
    bool RaycastBox(
        Vec2 origin,
        Vec2 direction,
        float distance,
        const ColliderGeometry& geometry,
        float& hitDistance,
        Vec2& hitNormal);
    bool RaycastCircle(
        Vec2 origin,
        Vec2 direction,
        float distance,
        const ColliderGeometry& geometry,
        float& hitDistance,
        Vec2& hitNormal);
    Component::BodyType2D GetBodyType(Canvas& canvas, GameObject* object);
    bool IntersectsBox(const Rect& area, const ColliderGeometry& geometry);
    bool IntersectsCircle(const Rect& area, const ColliderGeometry& geometry);
}
