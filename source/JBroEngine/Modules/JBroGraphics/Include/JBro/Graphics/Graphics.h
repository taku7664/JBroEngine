#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    // Temporary source-compatibility shell. W-host removes this after switching to Renderer.
    class GraphicsSystem final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

        bool AttachDevice(IRHIDevice* device);
        Renderer* GetRenderer();
    };
}
