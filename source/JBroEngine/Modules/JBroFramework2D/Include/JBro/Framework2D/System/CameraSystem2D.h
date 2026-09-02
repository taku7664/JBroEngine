#pragma once

#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
#include <JBro/Runtime/GameSystem.h>

namespace JBro::Engine
{
    class CameraSystem2D final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnExtractRender(CWorld& world, RenderWorld2D& renderWorld) override;

    private:
        Entity FindPrimaryCamera(CWorld& world) const;
        RenderCamera2D BuildRenderCamera(CWorld& world, Entity cameraEntity) const;
    };
}
