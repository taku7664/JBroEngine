#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Runtime/IFramework.h>
#include <JBro/Graphics/Graphics.h>
#include <JBro/Platform/Platform.h>
#include <JBro/RHI/RHI.h>

namespace JBro
{
    struct EngineConfig
    {
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
        bool enableValidation = false;
    };

    class EngineInstance
    {
    public:
        EngineInstance();
        ~EngineInstance();

        bool Initialize(const EngineConfig& config, IPlatform& platform, IRHIModule& rhi, IFramework& framework);
        bool Tick(float deltaTime);
        void RequestExit();
        void Shutdown();

        AssetManager* GetAssetManager();
        GraphicsSystem* GetGraphicsSystem();
        IFramework* GetFramework();
        bool IsRunning() const;

    private:
        bool CreateMainWindow();
        bool CreateGraphicsDevice();
        bool InitializeFramework();
        void BeginFrame();
        void UpdateFrame(float deltaTime);
        void RenderFrame();
        void EndFrame();

        EngineConfig m_config;
        IPlatform* m_platform = nullptr;
        IRHIModule* m_rhiModule = nullptr;
        IRHIDevice* m_device = nullptr;
        IFramework* m_framework = nullptr;
        WindowHandle m_mainWindow;
        AssetManager m_assets;
        GraphicsSystem m_graphics;
        bool m_running = false;
    };
}
