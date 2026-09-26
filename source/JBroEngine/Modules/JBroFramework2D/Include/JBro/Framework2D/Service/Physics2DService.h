#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro::Service
{
    // 스크립트가 물리에 묻는 값 서비스다(D-24). 뜻은 `System::IPhysics2DSystem` 과 같고, 레이어 마스크는 기본이 전부다.
    // Main-thread only. Uses the current SystemContext binding without owning it.
    class Physics2DService
    {
    public:
        // Replaces hit. A miss or an unavailable system clears the previous result.
        bool Raycast(Vec2 origin, Vec2 direction, float distance, RaycastHit2D& hit,
            std::uint32_t layerMask = AllPhysicsLayers) const;
        void RaycastAll(Vec2 origin, Vec2 direction, float distance, Array<RaycastHit2D>& hits,
            std::uint32_t layerMask = AllPhysicsLayers) const;

        // Replaces results with unique object handles. Reserve before repeated queries.
        // An unavailable system clears results without releasing the caller's capacity.
        void OverlapBox(const Rect& area, Array<GameObjectHandle>& results,
            std::uint32_t layerMask = AllPhysicsLayers) const;
        GameObjectHandle OverlapPoint(Vec2 point, std::uint32_t layerMask = AllPhysicsLayers) const;
        void OverlapCircle(Vec2 center, float radius, Array<GameObjectHandle>& results,
            std::uint32_t layerMask = AllPhysicsLayers) const;

        bool CircleCast(Vec2 origin, float radius, Vec2 direction, float distance, RaycastHit2D& hit,
            std::uint32_t layerMask = AllPhysicsLayers) const;
        bool BoxCast(Vec2 center, Vec2 halfExtents, float angle, Vec2 direction, float distance,
            RaycastHit2D& hit, std::uint32_t layerMask = AllPhysicsLayers) const;
    };
}
