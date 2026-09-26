#include <JBro/Framework2D/Service/Physics2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>

namespace JBro::Service
{
    bool Physics2DService::Raycast(
        Vec2 origin, Vec2 direction, float distance, RaycastHit2D& hit, std::uint32_t layerMask) const
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
        Vec2 origin, Vec2 direction, float distance, Array<RaycastHit2D>& hits, std::uint32_t layerMask) const
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
        std::uint32_t layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapBox(area, results, layerMask);
    }

    GameObjectHandle Physics2DService::OverlapPoint(Vec2 point, std::uint32_t layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            return {};
        }
        return physics->OverlapPoint(point, layerMask);
    }

    void Physics2DService::OverlapCircle(Vec2 center, float radius, Array<GameObjectHandle>& results,
        std::uint32_t layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapCircle(center, radius, results, layerMask);
    }

    bool Physics2DService::CircleCast(Vec2 origin, float radius, Vec2 direction, float distance,
        RaycastHit2D& hit, std::uint32_t layerMask) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->CircleCast(origin, radius, direction, distance, hit, layerMask);
    }

    bool Physics2DService::BoxCast(Vec2 center, Vec2 halfExtents, float angle, Vec2 direction, float distance,
        RaycastHit2D& hit, std::uint32_t layerMask) const
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
