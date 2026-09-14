#include <JBro/Editor/EditorApplication.h>

#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>

#include <imgui.h>

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

    bool EditorApplication::EnableEditorUi(const Extent2D& gameViewExtent)
    {
        if (false == m_initialized || m_uiEnabled
            || gameViewExtent.width == 0 || gameViewExtent.height == 0)
        {
            return false;
        }

        Renderer* renderer = m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        IRHIDevice* device = renderer->GetDevice();
        if (device == nullptr)
        {
            return false;
        }

        // 게임이 그려 넣고 UI 가 읽는 텍스처다. 둘 다 되어야 한다.
        TextureDesc desc;
        desc.extent = gameViewExtent;
        desc.format = renderer->GetBackBufferFormat();
        desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        m_gameView = device->CreateTexture(desc);
        if (false == m_gameView.IsValid())
        {
            return false;
        }

        if (false == m_ui.Initialize(*device, renderer->GetBackBufferFormat()))
        {
            device->DestroyTexture(m_gameView);
            m_gameView = {};
            return false;
        }

        FrameTarget target;
        target.texture = m_gameView;
        target.extent = gameViewExtent;
        if (false == m_engine->SetGameViewTarget(target)
            || false == renderer->SetFrameOverlay(&DrawEditorOverlay, this))
        {
            ReleaseEditorUi();
            return false;
        }

        m_gameViewExtent = gameViewExtent;
        m_uiEnabled = true;
        return true;
    }

    void EditorApplication::DisableEditorUi()
    {
        if (false == m_uiEnabled)
        {
            return;
        }
        ReleaseEditorUi();
    }

    bool EditorApplication::IsEditorUiEnabled() const
    {
        return m_uiEnabled;
    }

    TextureHandle EditorApplication::GetGameViewTexture() const
    {
        return m_gameView;
    }

    void EditorApplication::AbandonEditorUi()
    {
        // 엔진이 렌더 실패로 스스로 정리하면서 디바이스까지 지운 뒤다. 우리가 만든
        // 텍스처도 그때 함께 사라졌으므로 지우려 들지 않는다 - 죽은 디바이스로
        // DestroyTexture 를 부르면 그 자리에서 터진다.
        m_ui.AbandonDevice();
        m_gameView = {};
        m_gameViewExtent = {};
        m_uiEnabled = false;
    }

    void EditorApplication::ReleaseEditorUi()
    {
        // 게임을 백버퍼로 되돌리고 오버레이를 뗀다. 둘 중 하나만 하면 다음 프레임에
        // 사라진 UI 를 그리려 들거나 게임 화면이 버려진 텍스처로 간다.
        if (m_engine)
        {
            m_engine->SetGameViewTarget({});
            if (Renderer* renderer = m_engine->GetRenderer())
            {
                renderer->SetFrameOverlay(nullptr, nullptr);
                if (m_gameView.IsValid())
                {
                    if (IRHIDevice* device = renderer->GetDevice())
                    {
                        device->DestroyTexture(m_gameView);
                    }
                }
            }
        }
        m_ui.Shutdown();
        m_gameView = {};
        m_gameViewExtent = {};
        m_uiEnabled = false;
    }

    bool EditorApplication::BuildEditorUi(float deltaTime)
    {
        Renderer* renderer = m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        const Extent2D display = renderer->GetSurfaceExtent();
        if (display.width == 0 || display.height == 0)
        {
            // 창이 최소화됐다. 그릴 화면이 없으니 UI 도 만들지 않는다.
            return true;
        }

        // **여기서 한 번 더 펌프를 돈다.** 엔진의 Tick 도 펌프를 돌지만 그것은 UI 를
        // 다 만든 뒤라, 거기서 받은 입력은 다음 프레임에나 반영된다.
        m_platform->PumpEvents();
        if (false == m_ui.PushInput(m_platform->GetInputEvents()))
        {
            return false;
        }

        if (false == m_ui.BeginFrame(display, deltaTime))
        {
            return false;
        }

        // 게임 뷰 패널. **지금은 창을 통째로 채운다** - 도킹이 붙기 전까지는 패널이
        // 이것 하나뿐이고, 자리를 ImGui 기본값에 맡기면 작은 창에서 화면 밖으로 밀린다.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(display.width), static_cast<float>(display.height)));
        ImGui::Begin("Game", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoCollapse);
        // 텍스처 비율을 지키며 패널 안에 맞춘다(레터박스) - 늘려 붙이면 에디터 창
        // 모양에 따라 게임이 찌그러져 보인다.
        const ImVec2 panel = ImGui::GetContentRegionAvail();
        if (panel.x > 0.0f && panel.y > 0.0f && m_gameView.IsValid())
        {
            const float viewAspect = static_cast<float>(m_gameViewExtent.width)
                / static_cast<float>(m_gameViewExtent.height);
            const float panelAspect = panel.x / panel.y;
            ImVec2 size = panel;
            if (viewAspect > panelAspect)
            {
                size.y = panel.x / viewAspect;
            }
            else
            {
                size.x = panel.y * viewAspect;
            }
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(
                cursor.x + (panel.x - size.x) * 0.5f,
                cursor.y + (panel.y - size.y) * 0.5f));
            ImGui::Image(
                static_cast<ImTextureID>(EditorUI::ToTextureId(m_gameView)), size);
        }
        ImGui::End();

        // **텍스처와 버퍼는 여기서 올라간다. RHI 프레임 밖이어야 한다** -
        // 아래 엔진 Tick 이 프레임을 열고 나면 만들 수도 쓸 수도 없다.
        return m_ui.EndFrame();
    }

    bool EditorApplication::DrawEditorOverlay(
        IRHICommandContext& commands,
        TextureHandle backBuffer,
        std::uint32_t frameSlot,
        void* user)
    {
        auto* self = static_cast<EditorApplication*>(user);
        Renderer* renderer = self->m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        const Extent2D display = renderer->GetSurfaceExtent();

        ColorAttachmentDesc attachment;
        attachment.texture = backBuffer;
        attachment.loadOperation = LoadOperation::Clear;
        attachment.clearColor = {0.09f, 0.09f, 0.11f, 1.0f};
        RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }

        Viewport viewport;
        viewport.width = static_cast<float>(display.width);
        viewport.height = static_cast<float>(display.height);
        commands.SetViewport(viewport);

        const bool drawn = self->m_ui.Draw(commands, frameSlot);
        commands.EndRenderPass();
        return drawn;
    }

    bool EditorApplication::Tick(float deltaTime)
    {
        if (false == m_initialized || m_engine.Get() == nullptr)
        {
            return false;
        }
        // UI 를 먼저 만든다. 텍스처와 정점 버퍼가 RHI 프레임 **밖에서** 올라가야
        // 하는데, 엔진 Tick 이 그 프레임을 연다.
        if (m_uiEnabled && false == BuildEditorUi(deltaTime))
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
            // **엔진은 실패하면 그 자리에서 디바이스까지 놓는다.** UI 가 들고 있던
            // 파이프라인과 텍스처는 그때 함께 사라졌다 - 지우려 들면 터진다.
            if (m_uiEnabled)
            {
                AbandonEditorUi();
            }
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

    Renderer* EditorApplication::GetRenderer()
    {
        return m_engine ? m_engine->GetRenderer() : nullptr;
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
        // UI 가 잡은 GPU 리소스를 먼저 놓는다. 엔진이 디바이스를 지우고 나면
        // 그것들을 놓아 줄 길이 없다.
        ReleaseEditorUi();
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
