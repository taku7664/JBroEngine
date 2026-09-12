#include <JBro/Host/EngineInstance.h>

#include <cmath>
#include <cstdio>
#include <new>
#include <utility>

namespace JBro
{
    EngineInstance::EngineInstance() = default;

    EngineInstance::~EngineInstance()
    {
        Shutdown();
    }

    bool EngineInstance::Initialize(const EngineConfig& config, IPlatform& platform,
        IRHIModule& rhi)
    {
        if (m_state != State::Stopped || false == std::isfinite(config.fixedDeltaTime)
            || config.fixedDeltaTime <= 0.0f || config.maxFixedStepsPerFrame == 0)
        {
            return false;
        }
        m_state = State::Initializing;
        m_lastFrameStatus = FrameStatus::InvalidState;
        m_exitRequested = false;
        m_projectCloseRequested = false;
        m_scriptContextsBound = false;
        m_platform = &platform;
        try
        {
            m_mainWindow = platform.OpenPlatformWindow(config.window);
            WindowState windowState;
            if (m_mainWindow.value == 0 || platform.ShouldClose(m_mainWindow)
                || false == platform.GetWindowState(m_mainWindow, windowState))
            {
                ReleaseResources();
                return false;
            }
            RendererConfig rendererConfig;
            rendererConfig.api = config.graphicsApi;
            rendererConfig.surface = platform.CreateSurface(m_mainWindow);
            rendererConfig.surfaceExtent = {windowState.width, windowState.height};
            rendererConfig.validation = config.enableValidation;
            m_renderer = MakeOwnerPtr<Renderer>();
            if (false == m_renderer->Initialize(rhi, rendererConfig))
            {
                ReleaseResources();
                return false;
            }
            m_frameworkContext.memory = config.memory;
            m_frameworkContext.renderer = m_renderer.Get();
            m_frameworkContext.fixedDeltaTime = config.fixedDeltaTime;
            m_frameworkContext.maxFixedStepsPerFrame = config.maxFixedStepsPerFrame;
            if (m_exitRequested)
            {
                ReleaseResources();
                return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            ReleaseResources();
            return false;
        }
        catch (...)
        {
            ReleaseResources();
            throw;
        }
        m_state = State::Running;
        m_lastFrameStatus = FrameStatus::Ready;
        return true;
    }

    bool EngineInstance::OpenProject(IFramework& framework)
    {
        if (m_state != State::Running || m_framework != nullptr || m_exitRequested)
        {
            return false;
        }
        m_state = State::OpeningProject;
        m_lastFrameStatus = FrameStatus::InvalidState;
        bool initialized = false;
        try
        {
            m_assets = MakeOwnerPtr<AssetSystem>();
            if (m_assets->Initialize(m_frameworkContext.memory))
            {
                m_frameworkContext.assets = m_assets.Get();
                m_framework = &framework;
                initialized = framework.Initialize(m_frameworkContext);
                if (initialized && false == m_projectCloseRequested && false == m_exitRequested)
                {
                    m_scriptContextsBound = framework.BindScriptContexts();
                    initialized = m_scriptContextsBound;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            // Project allocation failure does not invalidate process resources.
        }
        catch (...)
        {
            m_state = State::Running;
            CloseProject();
            throw;
        }
        m_state = State::Running;
        if (false == initialized || m_projectCloseRequested || m_exitRequested)
        {
            CloseProject();
            return false;
        }
        m_lastFrameStatus = FrameStatus::Ready;
        return true;
    }

    void EngineInstance::CloseProject()
    {
        if (m_state == State::OpeningProject || m_state == State::Ticking)
        {
            m_projectCloseRequested = true;
            return;
        }
        if (m_state != State::Running)
        {
            return;
        }
        ReleaseProject();
        if (m_exitRequested)
        {
            ReleaseResources();
        }
    }

    bool EngineInstance::Tick(float deltaTime)
    {
        if (m_state != State::Running)
        {
            return false;
        }
        m_state = State::Ticking;
        m_lastFrameStatus = FrameStatus::Ready;
        try
        {
            if (false == TickFrame(deltaTime))
            {
                ReleaseResources();
                return false;
            }
        }
        catch (...)
        {
            m_lastFrameStatus = FrameStatus::InvalidState;
            ReleaseResources();
            throw;
        }
        m_state = State::Running;
        if (m_projectCloseRequested)
        {
            CloseProject();
        }
        return m_state == State::Running;
    }

    bool EngineInstance::TickFrame(float deltaTime)
    {
        m_platform->PumpEvents();
        if (m_exitRequested || m_platform->ShouldClose(m_mainWindow))
        {
            return false;
        }
        if (m_renderer->IsDeviceLost())
        {
            m_lastFrameStatus = FrameStatus::DeviceLost;
            return false;
        }
        if (false == std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            m_lastFrameStatus = FrameStatus::InvalidState;
            return false;
        }
        if (m_framework != nullptr && false == m_projectCloseRequested)
        {
            m_framework->Update(deltaTime);
        }
        if (m_exitRequested)
        {
            return false;
        }
        WindowState windowState;
        if (false == m_platform->GetWindowState(m_mainWindow, windowState))
        {
            m_lastFrameStatus = FrameStatus::SurfaceLost;
            return false;
        }
        if (m_framework == nullptr || m_projectCloseRequested
            || windowState.minimized || windowState.width == 0 || windowState.height == 0)
        {
            m_lastFrameStatus = FrameStatus::Skipped;
            return true;
        }
        const auto previousExtent = m_renderer->GetSurfaceExtent();
        if ((previousExtent.width != windowState.width || previousExtent.height != windowState.height)
            && false == m_renderer->ResizeSurface({windowState.width, windowState.height}))
        {
            m_lastFrameStatus = m_renderer->IsDeviceLost() ? FrameStatus::DeviceLost : FrameStatus::SurfaceLost;
            return false;
        }
        const auto beginStatus = m_renderer->BeginFrame();
        m_lastFrameStatus = beginStatus;
        if (beginStatus == FrameStatus::Skipped)
        {
            return true;
        }
        if (beginStatus != FrameStatus::Ready)
        {
            return false;
        }
        const bool submitted = m_framework->Render();
        if (false == submitted || m_exitRequested)
        {
            m_lastFrameStatus = submitted ? FrameStatus::Ready : FrameStatus::InvalidState;
            m_renderer->AbortFrame();
            return false;
        }
        if (m_projectCloseRequested)
        {
            m_renderer->AbortFrame();
            m_lastFrameStatus = FrameStatus::Skipped;
            return true;
        }
        const auto endStatus = m_renderer->EndFrame();
        m_lastFrameStatus = endStatus;
        return endStatus == FrameStatus::Ready || endStatus == FrameStatus::Skipped;
    }

    void EngineInstance::RequestExit()
    {
        m_exitRequested = true;
    }

    void EngineInstance::Shutdown()
    {
        if (m_state == State::Initializing || m_state == State::OpeningProject
            || m_state == State::ClosingProject || m_state == State::Ticking)
        {
            RequestExit();
            return;
        }
        if (m_state != State::Stopped && m_state != State::Stopping)
        {
            ReleaseResources();
        }
    }

    void EngineInstance::ReleaseProject()
    {
        const State previousState = std::exchange(m_state, State::ClosingProject);
        if (m_renderer)
        {
            m_renderer->AbortFrame();
        }
        if (auto* framework = std::exchange(m_framework, nullptr))
        {
            if (std::exchange(m_scriptContextsBound, false))
            {
                framework->UnbindScriptContexts();
            }
            try
            {
                framework->Shutdown();
            }
            catch (...)
            {
                // Cleanup hooks must not throw. Still release the GPU before its surface.
                std::fputs("JBro error: framework shutdown threw during host cleanup.\n", stderr);
                m_lastFrameStatus = FrameStatus::InvalidState;
                m_exitRequested = true;
            }
        }
        if (m_assets)
        {
            m_assets->Shutdown();
            m_assets.Reset();
        }
        m_frameworkContext.assets = nullptr;
        m_projectCloseRequested = false;
        m_state = previousState;
    }

    void EngineInstance::ReleaseResources()
    {
        m_state = State::Stopping;
        m_exitRequested = true;
        ReleaseProject();
        if (m_renderer)
        {
            m_renderer->Shutdown();
            m_renderer.Reset();
        }
        if (m_platform != nullptr && m_mainWindow.value != 0)
        {
            m_platform->ClosePlatformWindow(m_mainWindow);
        }
        m_mainWindow = {};
        m_platform = nullptr;
        m_frameworkContext = {};
        m_state = State::Stopped;
    }

    AssetSystem* EngineInstance::GetAssetSystem()
    {
        return m_assets.Get();
    }

    Renderer* EngineInstance::GetRenderer()
    {
        return m_renderer.Get();
    }

    IFramework* EngineInstance::GetFramework()
    {
        return m_framework;
    }

    bool EngineInstance::IsRunning() const
    {
        return (m_state == State::Running || m_state == State::Ticking) && false == m_exitRequested;
    }

    FrameStatus EngineInstance::GetLastFrameStatus() const
    {
        return m_lastFrameStatus;
    }
}
