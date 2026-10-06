#include <JBro/Framework2D/Service/Physics2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    Bool Physics2DService::Raycast(
        Vector2 origin, Vector2 direction, Float distance, RaycastHit2D& hit, UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->Raycast(origin, direction, distance, hit, layerMask);
    }

    void Physics2DService::RaycastAll(
        Vector2 origin, Vector2 direction, Float distance, Array<RaycastHit2D>& hits, UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hits.Clear();
            return;
        }
        physics->RaycastAll(origin, direction, distance, hits, layerMask);
    }

    void Physics2DService::OverlapBox(const Rect& area, Array<GameObjectHandle>& results,
        UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapBox(area, results, layerMask);
    }

    GameObjectHandle Physics2DService::OverlapPoint(Vector2 point, UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapPoint(point, layerMask);
    }

    void Physics2DService::OverlapCircle(Vector2 center, Float radius, Array<GameObjectHandle>& results,
        UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapCircle(center, radius, results, layerMask);
    }

    Bool Physics2DService::CircleCast(Vector2 origin, Float radius, Vector2 direction, Float distance,
        RaycastHit2D& hit, UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->CircleCast(origin, radius, direction, distance, hit, layerMask);
    }

    Bool Physics2DService::BoxCast(Vector2 center, Vector2 halfExtents, Float angle, Vector2 direction, Float distance,
        RaycastHit2D& hit, UInt32 layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->BoxCast(center, halfExtents, angle, direction, distance, hit, layerMask);
    }
}
