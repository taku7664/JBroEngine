#include <JBro/Framework2D/Service/Physics2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>

namespace JBro::Service
{
    bool Physics2DService::Raycast(
        Vec2 origin, Vec2 direction, float distance, Collision2D& hit) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }

        return physics->Raycast(origin, direction, distance, hit);
    }

    void Physics2DService::OverlapBox(const Rect& area, Array<GameObjectHandle>& results) const
    {
        System::IPhysics2DSystem* physics = GetFramework2DSystems().Physics2D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }

        physics->OverlapBox(area, results);
    }
}
