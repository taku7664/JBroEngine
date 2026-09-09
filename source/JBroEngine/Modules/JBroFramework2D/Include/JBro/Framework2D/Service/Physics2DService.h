#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Types/Array.h>

namespace JBro::Service
{
    // Main-thread only. Uses the current SystemContext binding without owning it.
    class Physics2DService
    {
    public:
        // Replaces hit. A miss or an unavailable system clears the previous result.
        bool Raycast(Vec2 origin, Vec2 direction, float distance, Collision2D& hit) const;

        // Replaces results with unique object handles. Reserve before repeated queries.
        // An unavailable system clears results without releasing the caller's capacity.
        void OverlapBox(const Rect& area, Array<GameObjectHandle>& results) const;
    };
}
