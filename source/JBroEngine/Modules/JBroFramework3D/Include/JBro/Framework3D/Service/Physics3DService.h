#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Types/Array.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 스크립트가 3D 물리에 묻는 값 서비스다(D-417, 2D 의 `Physics2DService` 와 같은 자리·같은 이름). 뜻은 `System::IPhysics3DSystem` 과 같고,
    // 레이어 마스크는 기본이 전부다. `GetFramework3DServices().Physics3D` 로 쓴다.
    // Main-thread only. Uses the current SystemContext binding without owning it.
    class Physics3DService
    {
    public:
        // Replaces hit. A miss or an unavailable system clears the previous result.
        Bool Raycast(Vector3 origin, Vector3 direction, Float distance, RaycastHit3D& hit, UInt32 layerMask = AllPhysicsLayers) const;
        void RaycastAll(Vector3 origin, Vector3 direction, Float distance, Array<RaycastHit3D>& hits,
            UInt32 layerMask = AllPhysicsLayers) const;

        // Replaces results with unique object handles. Reserve before repeated queries.
        void OverlapSphere(Vector3 center, Float radius, Array<Handle::GameObject>& results, UInt32 layerMask = AllPhysicsLayers) const;
        void OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, Array<Handle::GameObject>& results,
            UInt32 layerMask = AllPhysicsLayers) const;

        Bool SphereCast(Vector3 origin, Float radius, Vector3 direction, Float distance, RaycastHit3D& hit,
            UInt32 layerMask = AllPhysicsLayers) const;
        Bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, Float distance, RaycastHit3D& hit,
            UInt32 layerMask = AllPhysicsLayers) const;
    };
}
