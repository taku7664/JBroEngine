#pragma once

#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
#include <JBro/Runtime/GameSystem.h>

namespace JBro::Engine
{
    class SpriteRenderSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnExtractRender(CWorld& world, RenderWorld2D& renderWorld) override;

    private:
        SpriteRenderItem BuildRenderItem(CWorld& world, Entity entity) const;
        bool ShouldSubmit(CWorld& world, Entity entity) const;
    };
}
