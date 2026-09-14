#pragma once

#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class Canvas;
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

        // `.jproject` 파일을 읽어 연다. 그 파일이 가리키는 스크립트 DLL 까지 실린다.
        //
        // **차원은 인자로 받는다.** `.jproject` 에는 2D 인지 3D 인지 적는 자리가 없다 —
        // 기존 엔진이 2D 전용이라 그 키가 아예 없고, 없는 키를 여기서 지어내면
        // 그 쪽 프로젝트를 열 때 무엇을 적어야 할지 모르게 된다.
        bool OpenProjectFile(
            const char* projectFilePath,
            FrameworkKind framework,
            ProjectFileError& error);

        // 마지막으로 연 `.jproject` 의 내용이다. 파일로 열지 않았으면 기본값이다.
        const ProjectFile& GetProjectFile() const;
        // 프로젝트 파일이 있는 폴더를 기준으로 상대경로를 푼다.
        // `.jproject` 의 `LastOpenedCanvasPath` 처럼 그 파일 안의 경로가 전부 상대다.
        String ResolveProjectPath(const char* relativePath) const;

        bool Tick(float deltaTime);
        void CloseProject();
        void Shutdown();

        // 열려 있는 프로젝트의 캔버스다. 없으면 nullptr 이다.
        Canvas* GetCanvas();
        // 열려 있는 캔버스로 `.jcanvas` 를 읽고 쓴다.
        // 읽기는 **빈 캔버스에만** 들어간다 — 이미 내용이 있으면 거절한다.
        bool LoadCanvas(const char* path, CanvasFileError& error);
        bool SaveCanvas(const char* path, CanvasFileError& error);

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
        // 만든 쪽이 무엇을 만들었는지 기억한다. `IFramework` 에는 캔버스로 가는 길이 없고,
        // 그것을 뚫으려면 호스트 계층이 `Canvas` 를 보아야 한다(D-42 가 막는 방향이다).
        FrameworkKind m_frameworkKind = FrameworkKind::Framework2D;
        // 상대경로를 풀 기준이다. 파일로 열었을 때만 채워진다.
        String m_projectFilePath;
        GraphicsApi m_graphicsApi = GraphicsApi::D3D12;
        FrameStatus m_lastFrameStatus = FrameStatus::InvalidState;
        bool m_initialized = false;
    };
}
