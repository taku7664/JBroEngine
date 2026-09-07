#pragma once

namespace JBro
{
    class Renderer;
    class RenderWorld2D;
    namespace Internal
    {
        // Submits views inside a host-owned Renderer frame.
        bool SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer);
    }
}
