#include <JBro/Editor/EditorApplication.h>

#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2D/Framework2D.h>
#include <JBro/Framework3D/Framework3D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/EngineInstance.h>

#include <cmath>
#include <new>

namespace JBro
{
    EditorApplication::EditorApplication() = default;

    EditorApplication::~EditorApplication()
    {
        Shutdown();
    }

    bool EditorApplication::Initialize(const EditorApplicationConfig& config)
    {
        if (m_initialized || config.windowWidth == 0 || config.windowHeight == 0
            || false == std::isfinite(config.fixedDeltaTime) || config.fixedDeltaTime <= 0.0f
            || config.maxFixedStepsPerFrame == 0 || config.graphicsApi != GraphicsApi::D3D12)
        {
            return false;
        }

        try
        {
            m_platform = MakeOwnerPtr<WindowsPlatform>();
            if (false == m_platform->Initialize(config.memory))
            {
                ReleaseProcessResources();
                return false;
            }

            m_rhiModule = MakeOwnerPtr<D3D12RHIModule>();
            if (false == m_rhiModule->Initialize(config.memory))
            {
                ReleaseProcessResources();
                return false;
            }

            EngineConfig engineConfig;
            engineConfig.graphicsApi = config.graphicsApi;
            engineConfig.fixedDeltaTime = config.fixedDeltaTime;
            engineConfig.maxFixedStepsPerFrame = config.maxFixedStepsPerFrame;
            engineConfig.enableValidation = config.enableValidation;
            engineConfig.window.title = {"JBro Editor", 11};
            engineConfig.window.width = config.windowWidth;
            engineConfig.window.height = config.windowHeight;
            engineConfig.window.visible = config.windowVisible;
            engineConfig.memory = config.memory;

            m_engine = MakeOwnerPtr<EngineInstance>();
            if (false == m_engine->Initialize(engineConfig, *m_platform, *m_rhiModule))
            {
                ReleaseProcessResources();
                return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            ReleaseProcessResources();
            return false;
        }
        catch (...)
        {
            ReleaseProcessResources();
            throw;
        }

        m_graphicsApi = config.graphicsApi;
        m_lastFrameStatus = FrameStatus::Ready;
        m_initialized = true;
        return true;
    }

    bool EditorApplication::OpenProject(const ProjectDescriptor& project)
    {
        if (false == m_initialized || m_framework || project.graphicsApi != m_graphicsApi)
        {
            return false;
        }
        if (false == CreateSelectedFramework(project.framework))
        {
            return false;
        }
        bool opened = false;
        try
        {
            opened = m_engine->OpenProject(*m_framework);
        }
        catch (...)
        {
            DestroySelectedFramework();
            throw;
        }
        if (false == opened)
        {
            DestroySelectedFramework();
            return false;
        }
        return true;
    }

    bool EditorApplication::Tick(float deltaTime)
    {
        if (false == m_initialized || m_engine.Get() == nullptr)
        {
            return false;
        }
        const bool running = m_engine->Tick(deltaTime);
        m_lastFrameStatus = m_engine->GetLastFrameStatus();
        if (m_framework && m_engine->GetFramework() == nullptr)
        {
            DestroySelectedFramework();
        }
        if (false == running)
        {
            ReleaseProcessResources();
            return false;
        }
        return true;
    }

    void EditorApplication::CloseProject()
    {
        if (false == m_initialized || m_framework.Get() == nullptr)
        {
            return;
        }
        m_engine->CloseProject();
        m_lastFrameStatus = m_engine->GetLastFrameStatus();
        if (m_engine->GetFramework() == nullptr)
        {
            DestroySelectedFramework();
        }
    }

    void EditorApplication::Shutdown()
    {
        ReleaseProcessResources();
    }

    bool EditorApplication::IsInitialized() const
    {
        return m_initialized;
    }

    bool EditorApplication::HasOpenProject() const
    {
        return static_cast<bool>(m_framework);
    }

    FrameStatus EditorApplication::GetLastFrameStatus() const
    {
        return m_lastFrameStatus;
    }

    bool EditorApplication::CreateSelectedFramework(FrameworkKind framework)
    {
        try
        {
            switch (framework)
            {
            case FrameworkKind::Framework2D:
                m_framework = MakeOwnerPtr<Framework2D>();
                return true;
            case FrameworkKind::Framework3D:
                m_framework = MakeOwnerPtr<Framework3D>();
                return true;
            }
        }
        catch (const std::bad_alloc&)
        {
            m_framework.Reset();
            return false;
        }
        return false;
    }

    void EditorApplication::DestroySelectedFramework()
    {
        m_framework.Reset();
    }

    void EditorApplication::ReleaseProcessResources()
    {
        if (m_engine)
        {
            m_engine->Shutdown();
            m_lastFrameStatus = m_engine->GetLastFrameStatus();
        }
        DestroySelectedFramework();
        m_engine.Reset();
        if (m_rhiModule)
        {
            m_rhiModule->Shutdown();
            m_rhiModule.Reset();
        }
        if (m_platform)
        {
            m_platform->Shutdown();
            m_platform.Reset();
        }
        m_initialized = false;
    }
}
