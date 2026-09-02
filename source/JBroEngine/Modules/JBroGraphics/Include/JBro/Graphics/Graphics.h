#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/RHI/RHI.h>

namespace JBro::Engine
{
    struct TextureAsset
    {
        AssetId id;
        TextureHandle texture;
    };

    struct ShaderAsset
    {
        AssetId id;
        JStringView entryPoint;
    };

    class Renderer
    {
    public:
        bool Initialize(IRHIDevice* device);
        void BeginFrame();
        void Render();
        void EndFrame();
        void Shutdown();
    };

    class GraphicsSystem final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

        bool AttachDevice(IRHIDevice* device);
        Renderer* GetRenderer();
    };
}
