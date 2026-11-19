#include <JBro/Framework3D/Service/Physics3DService.h>

#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/System/IPhysics3DSystem.h>

namespace JBro::Service
{
    Bool Physics3DService::Raycast(Vector3 origin, Vector3 direction, Float distance, RaycastHit3D& hit, UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->Raycast(origin, direction, distance, hit, layerMask);
    }

    void Physics3DService::RaycastAll(Vector3 origin, Vector3 direction, Float distance, Array<RaycastHit3D>& hits, UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            hits.Clear();
            return;
        }
        physics->RaycastAll(origin, direction, distance, hits, layerMask);
    }

    void Physics3DService::OverlapSphere(Vector3 center, Float radius, Array<Handle::GameObject>& results, UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapSphere(center, radius, results, layerMask);
    }

    void Physics3DService::OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, Array<Handle::GameObject>& results,
        UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            results.Clear();
            return;
        }
        physics->OverlapBox(center, halfExtents, rotation, results, layerMask);
    }

    Bool Physics3DService::SphereCast(Vector3 origin, Float radius, Vector3 direction, Float distance, RaycastHit3D& hit,
        UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->SphereCast(origin, radius, direction, distance, hit, layerMask);
    }

    Bool Physics3DService::BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, Float distance,
        RaycastHit3D& hit, UInt32 layerMask) const
    {
        System::IPhysics3DSystem* physics = GetFramework3DSystems().Physics3D;
        if (physics == nullptr)
        {
            hit = {};
            return false;
        }
        return physics->BoxCast(center, halfExtents, rotation, direction, distance, hit, layerMask);
    }
}
