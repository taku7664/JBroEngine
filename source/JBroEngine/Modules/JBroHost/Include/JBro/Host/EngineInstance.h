#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/Platform.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/LinearAllocator.h>

namespace JBro
{
    struct EngineConfig
    {
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
        // 프레임 임시 메모리 예산이다(D-52). memory.frame 을 직접 채워 주면 그것을 그대로 쓰고,
        // 비어 있으면 호스트가 이 크기로 선형 할당기를 만들어 채운다. 0 이면 만들지 않는다.
        std::size_t frameMemoryBytes = 1u << 20;
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

        AssetSystem* GetAssetSystem();
        Renderer* GetRenderer();
        // 호스트가 만든 프레임 아레나다. 호출자가 memory.frame 을 직접 채웠으면 null 이다.
        LinearAllocator* GetFrameMemory();
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
        OwnerPtr<AssetSystem> m_assets;
        OwnerPtr<Renderer> m_renderer;
        // 프레임 경계에서 되감는다. m_frameworkContext.memory.frame 이 이것을 가리킨다.
        OwnerPtr<LinearAllocator> m_frameMemory;
        FrameworkContext m_frameworkContext;
        State m_state = State::Stopped;
        bool m_exitRequested = false;
        bool m_projectCloseRequested = false;
        bool m_scriptContextsBound = false;
        FrameStatus m_lastFrameStatus = FrameStatus::Ready;
    };
}
