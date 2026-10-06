#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 스크립트가 물리에 묻는 값 서비스다(D-24). 뜻은 `System::IPhysics2DSystem` 과 같고, 레이어 마스크는 기본이 전부다.
    // Main-thread only. Uses the current SystemContext binding without owning it.
    class Physics2DService
    {
    public:
        // Replaces hit. A miss or an unavailable system clears the previous result.
        Bool Raycast(Vector2 origin, Vector2 direction, Float distance, RaycastHit2D& hit,
            UInt32 layerMask = AllPhysicsLayers) const;
        void RaycastAll(Vector2 origin, Vector2 direction, Float distance, Array<RaycastHit2D>& hits,
            UInt32 layerMask = AllPhysicsLayers) const;

        // Replaces results with unique object handles. Reserve before repeated queries.
        // An unavailable system clears results without releasing the caller's capacity.
        void OverlapBox(const Rect& area, Array<GameObjectHandle>& results,
            UInt32 layerMask = AllPhysicsLayers) const;
        GameObjectHandle OverlapPoint(Vector2 point, UInt32 layerMask = AllPhysicsLayers) const;
        void OverlapCircle(Vector2 center, Float radius, Array<GameObjectHandle>& results,
            UInt32 layerMask = AllPhysicsLayers) const;

        Bool CircleCast(Vector2 origin, Float radius, Vector2 direction, Float distance, RaycastHit2D& hit,
            UInt32 layerMask = AllPhysicsLayers) const;
        Bool BoxCast(Vector2 center, Vector2 halfExtents, Float angle, Vector2 direction, Float distance,
            RaycastHit2D& hit, UInt32 layerMask = AllPhysicsLayers) const;
    };
}
