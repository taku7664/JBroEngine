#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Runtime/IFramework.h>
#include <JBro/Graphics/Renderer.h>
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
        WindowDesc window;
        JMemoryContext memory;
    };

    class EngineInstance
    {
    public:
        EngineInstance();
        ~EngineInstance();
        EngineInstance(const EngineInstance&) = delete;
        EngineInstance& operator=(const EngineInstance&) = delete;
        EngineInstance(EngineInstance&&) = delete;
        EngineInstance& operator=(EngineInstance&&) = delete;

        // Main-thread only. Modules and an initially stopped framework are borrowed;
        // their objects must outlive this instance. The host owns their project session.
        bool Initialize(const EngineConfig& config, IPlatform& platform, IRHIModule& rhi, IFramework& framework);
        // Pumps events, updates simulation, then renders. False means stopped and cleaned up.
        // Recursive Tick calls are rejected without changing the outer frame.
        bool Tick(float deltaTime);
        void RequestExit();
        // Callback calls defer teardown until that callback returns.
        void Shutdown();

        AssetManager* GetAssetManager();
        Renderer* GetRenderer();
        IFramework* GetFramework();
        bool IsRunning() const;

    private:
        enum class State { Stopped, Initializing, Running, Ticking, Stopping };
        bool TickFrame(float deltaTime);
        void ReleaseResources();

        IPlatform* m_platform = nullptr;
        IFramework* m_framework = nullptr;
        WindowHandle m_mainWindow;
        OwnerPtr<AssetManager> m_assets;
        OwnerPtr<Renderer> m_renderer;
        State m_state = State::Stopped;
        bool m_exitRequested = false;
    };
}
