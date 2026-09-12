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
            // 호스트가 프레임을 열고 닫으므로 프레임 메모리도 호스트가 소유하고 되감는다.
            // D-52 는 Canvas::BeginFrame 을 적었지만 Canvas 는 이 메모리를 소유하지 않는다.
            if (m_frameworkContext.memory.frame.allocate == nullptr && config.frameMemoryBytes > 0)
            {
                m_frameMemory = MakeOwnerPtr<LinearAllocator>();
                if (false == m_frameMemory->Initialize(config.frameMemoryBytes))
                {
                    m_frameMemory.Reset();
                    return false;
                }
                m_frameworkContext.memory.frame = m_frameMemory->GetInterface();
            }
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
        return OpenProject(framework, nullptr);
    }

    bool EngineInstance::OpenProjectFile(
        IFramework& framework,
        const char* projectFilePath,
        ProjectFileError& error)
    {
        ProjectFile project;
        if (false == LoadProjectFile(projectFilePath, project, error))
        {
            return false;
        }
        const String modulePath = ResolveScriptModulePath(project, projectFilePath);
        if (false == OpenProject(framework, modulePath.c_str()))
        {
            return false;
        }
        m_project = project;
        return true;
    }

    const ProjectFile& EngineInstance::GetProjectFile() const
    {
        return m_project;
    }

    bool EngineInstance::OpenProject(IFramework& framework, const char* scriptModulePath)
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
                    // 컨텍스트가 붙은 뒤에 싣는다. 로더가 그 컨텍스트를 읽어 DLL 에 넘긴다.
                    if (initialized && scriptModulePath != nullptr && scriptModulePath[0] != 0)
                    {
                        const JArrayView<ScriptContextBlock> blocks =
                            framework.GetScriptContextBlocks();
                        initialized = m_scripts.Load(
                            scriptModulePath, *m_platform, blocks.data, blocks.size);
                    }
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
        // 프레임의 시작에서 되감는다. 지난 프레임이 나눠 준 포인터는 여기서 전부 무효가 된다.
        if (m_frameMemory)
        {
            m_frameMemory->Reset();
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
        const RenderResult renderResult = m_framework->Render();
        if (renderResult == RenderResult::Failed || m_exitRequested)
        {
            m_lastFrameStatus = renderResult == RenderResult::Failed
                ? FrameStatus::InvalidState
                : FrameStatus::Ready;
            m_renderer->AbortFrame();
            return false;
        }
        // 그릴 것이 없는 프레임은 버리되, 루프는 살아있다(F-7).
        if (renderResult == RenderResult::NothingToSubmit || m_projectCloseRequested)
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
            // DLL 이 먼저 내려간다. 그 뒤에야 DLL 이 붙잡고 있던 컨텍스트를 풀 수 있다.
            if (m_platform != nullptr)
            {
                m_scripts.Unload(*m_platform);
            }
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
        // 컨텍스트를 비우기 전에 아레나를 접는다. memory.frame 이 이것을 가리키고 있었다.
        m_frameMemory.Reset();
        m_project = {};
        m_frameworkContext = {};
        m_state = State::Stopped;
    }

    AssetSystem* EngineInstance::GetAssetSystem()
    {
        return m_assets.Get();
    }

    LinearAllocator* EngineInstance::GetFrameMemory()
    {
        return m_frameMemory.Get();
    }

    const ScriptDLLLoader& EngineInstance::GetScriptModule() const
    {
        return m_scripts;
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
