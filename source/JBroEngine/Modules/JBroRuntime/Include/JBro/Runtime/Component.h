#pragma once

#include <JBro/Core/Core.h>

namespace JBro::Engine
{
    // Runtime component behavior is intentionally deferred. Engine-side
    // allocation lives in JBro/Core/ECS and does not require this base class.
    class Component
    {
    public:
        virtual ~Component() = default;
    };
}
