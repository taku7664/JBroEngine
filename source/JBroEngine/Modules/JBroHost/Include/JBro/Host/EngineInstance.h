#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Host/ScriptDLLLoader.h>
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
        // 같은 것을 열되 이 프로젝트의 스크립트 DLL 도 함께 싣는다.
        // 경로가 어느 파일에서 오는지는 프로젝트 파일 형식의 문제이고 아직 정해지지 않았다.
        // 호스트는 그저 경로를 받는다 — 그 결정이 나도 이 배선은 그대로다.
        // 비거나 null 이면 스크립트 없이 여는 것과 같다. DLL 이 실패하면 프로젝트가 열리지 않는다.
        bool OpenProject(IFramework& framework, const char* scriptModulePath);
        // `.jproject` 를 읽고 그것이 가리키는 스크립트 모듈까지 실어서 연다.
        // 프로젝트 파일을 읽지 못하면 아무것도 열지 않고 error 를 채운다.
        bool OpenProjectFile(
            IFramework& framework,
            const char* projectFilePath,
            ProjectFileError& error);
        // 마지막으로 연 프로젝트 파일의 내용이다.
        const ProjectFile& GetProjectFile() const;
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
        // 이 프로젝트에 실린 스크립트 DLL. 열리지 않았으면 아무것도 싣지 않은 상태다.
        const ScriptDLLLoader& GetScriptModule() const;
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
        // 프로젝트 수명이다. 컨텍스트 바인딩 뒤에 싣고, 해제 전에 내린다.
        ScriptDLLLoader m_scripts;
        ProjectFile m_project;
        FrameworkContext m_frameworkContext;
        State m_state = State::Stopped;
        bool m_exitRequested = false;
        bool m_projectCloseRequested = false;
        bool m_scriptContextsBound = false;
        FrameStatus m_lastFrameStatus = FrameStatus::Ready;
    };
}
