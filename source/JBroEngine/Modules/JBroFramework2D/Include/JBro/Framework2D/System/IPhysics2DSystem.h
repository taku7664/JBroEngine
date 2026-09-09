#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Types/Array.h>

namespace JBro::System
{
    class IPhysics2DSystem
    {
    public:
        virtual ~IPhysics2DSystem() = default;

        // Main-thread only. Queries use the canvas bound during system initialization.
        // Raycast replaces hit; a miss or an uninitialized system clears it.
        virtual bool Raycast(
            Vec2 origin,
            Vec2 direction,
            float distance,
            Collision2D& hit) const = 0;
        // Replaces results, with each object included once. Reserve for repeated queries.
        // The caller owns the result storage; shutdown queries return no results.
        virtual void OverlapBox(
            const Rect& area,
            Array<GameObjectHandle>& results) const = 0;
    };
}
