#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro
{
    class EngineInstance;
    class IFramework;
    class IPlatform;

    enum class FrameworkKind : std::uint8_t
    {
        Framework2D,
        Framework3D
    };

    struct ProjectDescriptor
    {
        JStringView name;
        JStringView projectGuid;
        JStringView engineVersion;
        FrameworkKind framework = FrameworkKind::Framework2D;
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
    };

    struct EditorApplicationConfig
    {
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
        std::uint32_t windowWidth = 1280;
        std::uint32_t windowHeight = 720;
        bool windowVisible = true;
        bool enableValidation = false;
        JMemoryContext memory;
    };

    class EditorApplication
    {
    public:
        EditorApplication();
        ~EditorApplication();
        EditorApplication(const EditorApplication&) = delete;
        EditorApplication& operator=(const EditorApplication&) = delete;
        EditorApplication(EditorApplication&&) = delete;
        EditorApplication& operator=(EditorApplication&&) = delete;

        bool Initialize(const EditorApplicationConfig& config);
        bool OpenProject(const ProjectDescriptor& project);
        bool Tick(float deltaTime);
        void CloseProject();
        void Shutdown();

        bool IsInitialized() const;
        bool HasOpenProject() const;
        FrameStatus GetLastFrameStatus() const;

    private:
        bool CreateSelectedFramework(FrameworkKind framework);
        void DestroySelectedFramework();
        void ReleaseProcessResources();

        OwnerPtr<IPlatform> m_platform;
        OwnerPtr<IRHIModule> m_rhiModule;
        OwnerPtr<EngineInstance> m_engine;
        OwnerPtr<IFramework> m_framework;
        GraphicsApi m_graphicsApi = GraphicsApi::D3D12;
        FrameStatus m_lastFrameStatus = FrameStatus::InvalidState;
        bool m_initialized = false;
    };
}
