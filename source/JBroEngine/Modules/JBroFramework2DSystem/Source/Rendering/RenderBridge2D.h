#pragma once

#include <JBro/Host/IFramework.h>

namespace JBro
{
    class Renderer;
    class RenderWorld2D;
    namespace Internal
    {
        // Submits views inside a host-owned Renderer frame.
        RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer);
    }
}
