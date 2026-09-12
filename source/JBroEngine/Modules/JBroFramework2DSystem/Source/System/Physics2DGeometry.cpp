#include "Physics2DGeometry.h"

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>

namespace JBro::Internal
{
    namespace
    {
        constexpr float GeometryEpsilon = 0.000001f;

        struct WorldPose
        {
            Matrix3x2 matrix;
            Vec2 scale{1.0f, 1.0f};
        };

        bool CalculateWorldPose(Canvas& canvas, GameObject* object, WorldPose& result)
        {
            Component::Transform2D* local =
                canvas.FindComponentRaw<Component::Transform2D>(object);
            if (local == nullptr || false == local->IsActiveComponent())
            {
                return false;
            }

            const Matrix3x2 localMatrix = MakeTransformMatrix2D(
                local->position,
                local->rotation,
                local->scale);
            GameObject* parent = object->GetParent();
            Component::Transform2D* parentLocal =
                canvas.FindComponentRaw<Component::Transform2D>(parent);
            if (parentLocal == nullptr || false == parentLocal->IsActiveComponent())
            {
                result.matrix = localMatrix;
                result.scale = local->scale;
                return true;
            }

            WorldPose parentPose;
            if (false == CalculateWorldPose(canvas, parent, parentPose))
            {
                return false;
            }
            result.matrix = MultiplyMatrix3x2(localMatrix, parentPose.matrix);
            result.scale = {
                local->scale.x * parentPose.scale.x,
                local->scale.y * parentPose.scale.y};
            return true;
        }

        bool IntersectRayAxis(
            float origin,
            float direction,
            float minimum,
            float maximum,
            float negativeNormal,
            float positiveNormal,
            float& enterDistance,
            float& exitDistance,
            float& enterNormal)
        {
            if (std::fabs(direction) <= GeometryEpsilon)
            {
                return origin >= minimum && origin <= maximum;
            }

            float first = (minimum - origin) / direction;
            float second = (maximum - origin) / direction;
            float normal = negativeNormal;
            if (first > second)
            {
                const float swapped = first;
                first = second;
                second = swapped;
                normal = positiveNormal;
            }

            if (first > enterDistance)
            {
                enterDistance = first;
                enterNormal = normal;
            }
            if (second < exitDistance)
            {
                exitDistance = second;
            }
            return enterDistance <= exitDistance;
        }

        void GetOrderedArea(
            const Rect& area,
            float& minimumX,
            float& maximumX,
            float& minimumY,
            float& maximumY)
        {
            minimumX = area.min.x < area.max.x ? area.min.x : area.max.x;
            maximumX = area.min.x > area.max.x ? area.min.x : area.max.x;
            minimumY = area.min.y < area.max.y ? area.min.y : area.max.y;
            maximumY = area.min.y > area.max.y ? area.min.y : area.max.y;
        }
    }

    bool CalculateColliderGeometry(
        Canvas& canvas,
        Component::Collider2D& collider,
        ColliderGeometry& result)
    {
        GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
        if (owner == nullptr)
        {
            return false;
        }

        WorldPose pose;
        if (false == CalculateWorldPose(canvas, owner, pose))
        {
            return false;
        }

        result.center.x =
            collider.offset.x * pose.matrix.m11
            + collider.offset.y * pose.matrix.m21
            + pose.matrix.m31;
        result.center.y =
            collider.offset.x * pose.matrix.m12
            + collider.offset.y * pose.matrix.m22
            + pose.matrix.m32;
        result.halfExtents = {
            std::fabs(collider.size.x * pose.scale.x) * 0.5f,
            std::fabs(collider.size.y * pose.scale.y) * 0.5f};
        const float scale =
            std::fabs(pose.scale.x) > std::fabs(pose.scale.y)
                ? std::fabs(pose.scale.x)
                : std::fabs(pose.scale.y);
        result.radius = std::fabs(collider.radius) * scale;
        return true;
    }

    bool RaycastBox(
        Vec2 origin,
        Vec2 direction,
        float distance,
        const ColliderGeometry& geometry,
        float& hitDistance,
        Vec2& hitNormal)
    {
        const Vec2 minimum = {
            geometry.center.x - geometry.halfExtents.x,
            geometry.center.y - geometry.halfExtents.y};
        const Vec2 maximum = {
            geometry.center.x + geometry.halfExtents.x,
            geometry.center.y + geometry.halfExtents.y};
        float enterDistance = 0.0f;
        float exitDistance = distance;
        float normalX = 0.0f;
        float normalY = 0.0f;
        if (false == IntersectRayAxis(
            origin.x,
            direction.x,
            minimum.x,
            maximum.x,
            -1.0f,
            1.0f,
            enterDistance,
            exitDistance,
            normalX))
        {
            return false;
        }

        const float beforeY = enterDistance;
        if (false == IntersectRayAxis(
            origin.y,
            direction.y,
            minimum.y,
            maximum.y,
            -1.0f,
            1.0f,
            enterDistance,
            exitDistance,
            normalY))
        {
            return false;
        }

        if (enterDistance <= 0.0f)
        {
            hitNormal = {-direction.x, -direction.y};
        }
        else if (enterDistance > beforeY)
        {
            hitNormal = {0.0f, normalY};
        }
        else
        {
            hitNormal = {normalX, 0.0f};
        }
        hitDistance = enterDistance;
        return enterDistance <= distance;
    }

    bool RaycastCircle(
        Vec2 origin,
        Vec2 direction,
        float distance,
        const ColliderGeometry& geometry,
        float& hitDistance,
        Vec2& hitNormal)
    {
        if (geometry.radius <= GeometryEpsilon)
        {
            return false;
        }

        const Vec2 offset = {
            origin.x - geometry.center.x,
            origin.y - geometry.center.y};
        const float offsetLengthSquared =
            offset.x * offset.x + offset.y * offset.y;
        const float radiusSquared = geometry.radius * geometry.radius;
        if (offsetLengthSquared <= radiusSquared)
        {
            hitDistance = 0.0f;
            hitNormal = {-direction.x, -direction.y};
            return true;
        }

        const float projection =
            offset.x * direction.x + offset.y * direction.y;
        const float discriminant =
            projection * projection - (offsetLengthSquared - radiusSquared);
        if (discriminant < 0.0f)
        {
            return false;
        }

        const float resultDistance = -projection - std::sqrt(discriminant);
        if (resultDistance < 0.0f || resultDistance > distance)
        {
            return false;
        }

        const Vec2 point = {
            origin.x + direction.x * resultDistance,
            origin.y + direction.y * resultDistance};
        hitDistance = resultDistance;
        hitNormal = {
            (point.x - geometry.center.x) / geometry.radius,
            (point.y - geometry.center.y) / geometry.radius};
        return true;
    }

    Component::BodyType2D GetBodyType(Canvas& canvas, GameObject* object)
    {
        Component::Rigidbody2D* body =
            canvas.FindComponentRaw<Component::Rigidbody2D>(object);
        if (body == nullptr)
        {
            return Component::BodyType2D::Static;
        }
        return body->bodyType;
    }

    bool IntersectsBox(const Rect& area, const ColliderGeometry& geometry)
    {
        float minimumX = 0.0f;
        float maximumX = 0.0f;
        float minimumY = 0.0f;
        float maximumY = 0.0f;
        GetOrderedArea(area, minimumX, maximumX, minimumY, maximumY);

        const float colliderMinimumX = geometry.center.x - geometry.halfExtents.x;
        const float colliderMaximumX = geometry.center.x + geometry.halfExtents.x;
        const float colliderMinimumY = geometry.center.y - geometry.halfExtents.y;
        const float colliderMaximumY = geometry.center.y + geometry.halfExtents.y;
        return colliderMaximumX >= minimumX
            && colliderMinimumX <= maximumX
            && colliderMaximumY >= minimumY
            && colliderMinimumY <= maximumY;
    }

    bool IntersectsCircle(const Rect& area, const ColliderGeometry& geometry)
    {
        float minimumX = 0.0f;
        float maximumX = 0.0f;
        float minimumY = 0.0f;
        float maximumY = 0.0f;
        GetOrderedArea(area, minimumX, maximumX, minimumY, maximumY);

        float closestX = geometry.center.x;
        float closestY = geometry.center.y;
        if (closestX < minimumX)
        {
            closestX = minimumX;
        }
        else if (closestX > maximumX)
        {
            closestX = maximumX;
        }
        if (closestY < minimumY)
        {
            closestY = minimumY;
        }
        else if (closestY > maximumY)
        {
            closestY = maximumY;
        }

        const float differenceX = geometry.center.x - closestX;
        const float differenceY = geometry.center.y - closestY;
        return differenceX * differenceX + differenceY * differenceY
            <= geometry.radius * geometry.radius;
    }
}
