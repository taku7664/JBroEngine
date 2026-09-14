#include <JBro/Editor/EditorApplication.h>

#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>

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
        m_frameworkKind = project.framework;
        return true;
    }

    bool EditorApplication::OpenProjectFile(
        const char* projectFilePath,
        FrameworkKind framework,
        ProjectFileError& error)
    {
        error = ProjectFileError{};
        if (false == m_initialized || m_framework)
        {
            error.message = "the editor is not ready for another project";
            return false;
        }
        if (false == CreateSelectedFramework(framework))
        {
            error.message = "the framework for this project could not be created";
            return false;
        }

        bool opened = false;
        try
        {
            opened = m_engine->OpenProjectFile(*m_framework, projectFilePath, error);
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
        m_frameworkKind = framework;
        m_projectFilePath = projectFilePath != nullptr ? projectFilePath : "";
        return true;
    }

    const ProjectFile& EditorApplication::GetProjectFile() const
    {
        return m_engine->GetProjectFile();
    }

    String EditorApplication::ResolveProjectPath(const char* relativePath) const
    {
        String relative(relativePath != nullptr ? relativePath : "");
        if (relative.empty())
        {
            return relative;
        }
        // 이미 절대경로면 그대로 둔다. `ResolveScriptModulePath` 와 같은 판정이다.
        const bool absolute = relative.size() > 1
            && (relative[0] == '/' || relative[0] == '\\' || relative[1] == ':');
        if (absolute || m_projectFilePath.empty())
        {
            return relative;
        }
        String directory(m_projectFilePath);
        const std::size_t slash = directory.find_last_of("/\\");
        if (slash == String::npos)
        {
            return relative;
        }
        directory.resize(slash + 1);
        directory.append(relative.c_str(), relative.size());
        return directory;
    }

    Canvas* EditorApplication::GetCanvas()
    {
        if (m_framework.Get() == nullptr)
        {
            return nullptr;
        }
        // 만든 쪽이 무엇을 만들었는지 안다. dynamic_cast 를 쓰지 않는 이유가 그것이다.
        switch (m_frameworkKind)
        {
        case FrameworkKind::Framework2D:
            return static_cast<Framework2D*>(m_framework.Get())->GetCanvas();
        case FrameworkKind::Framework3D:
            return static_cast<Framework3D*>(m_framework.Get())->GetCanvas();
        }
        return nullptr;
    }

    bool EditorApplication::LoadCanvas(const char* path, CanvasFileError& error)
    {
        error = CanvasFileError{};
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            error.message = "no project is open";
            return false;
        }
        return LoadCanvasFile(*canvas, path, error);
    }

    bool EditorApplication::SaveCanvas(const char* path, CanvasFileError& error)
    {
        error = CanvasFileError{};
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            error.message = "no project is open";
            return false;
        }
        return SaveCanvasFile(*canvas, path, error);
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
        // 상대경로 기준도 함께 지운다. 프로젝트가 없는데 남아 있으면
        // 다음 프로젝트의 경로가 옛 폴더를 기준으로 풀린다.
        m_projectFilePath.clear();
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
