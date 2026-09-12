#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Host/IFramework.h>
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

        // Main-thread only. Borrowed modules must outlive this instance.
        // Process resources are initialized once, independently of project sessions.
        bool Initialize(const EngineConfig& config, IPlatform& platform, IRHIModule& rhi);
        // The initially stopped framework object is borrowed until CloseProject returns.
        bool OpenProject(IFramework& framework);
        // Keeps the renderer/device/window alive. Calls from callbacks are deferred.
        void CloseProject();
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
        // Preserved after teardown; Ready/Skipped are non-fatal, other values indicate failure.
        FrameStatus GetLastFrameStatus() const;

    private:
        enum class State { Stopped, Initializing, OpeningProject, Running, Ticking, ClosingProject, Stopping };
        bool TickFrame(float deltaTime);
        void ReleaseProject();
        void ReleaseResources();

        IPlatform* m_platform = nullptr;
        IFramework* m_framework = nullptr;
        WindowHandle m_mainWindow;
        OwnerPtr<AssetManager> m_assets;
        OwnerPtr<Renderer> m_renderer;
        FrameworkContext m_frameworkContext;
        State m_state = State::Stopped;
        bool m_exitRequested = false;
        bool m_projectCloseRequested = false;
        bool m_scriptContextsBound = false;
        FrameStatus m_lastFrameStatus = FrameStatus::Ready;
    };
}
