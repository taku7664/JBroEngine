#include <JBro/Host/EngineInstance.h>

#include <JBro/Asset/AssetTypeRules.h>

#include <cmath>
#include <string_view>
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
            m_createMissingAssetMeta = config.createMissingAssetMeta;
            m_watchAssetDirectory = config.watchAssetDirectory;
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
        if (m_platform == nullptr)
        {
            error.line = 0;
            error.message = "the engine is not initialized";
            return false;
        }
        if (false == LoadProjectFile(*m_platform, projectFilePath, project, error))
        {
            return false;
        }
        const String modulePath = ResolveScriptModulePath(project, projectFilePath);
        if (false == OpenProject(framework, modulePath.c_str()))
        {
            // 파일은 읽혔는데 여는 데 실패한 것이다. 여기서 아무 말도 하지 않으면
            // 부르는 쪽은 빈 오류를 받고 무엇이 잘못됐는지 알 길이 없다.
            // 스크립트 DLL 은 여기에 없다 - 그것을 못 실은 것은 실패가 아니다(D-98).
            error.line = 0;
            error.message = "the project file was read but the project could not be opened";
            return false;
        }
        m_project = project;

        // 에셋 폴더를 한 번 스캔하고 에셋 시스템을 잇는다(D-111). **폴더가 없어도 프로젝트는 열린다** - 에셋이 하나도
        // 없는 새 프로젝트가 그것이다. 스캔 결과는 `GetAssetScanReport` 로 남는다.
        m_assetRoot = ResolveProjectRelativePath(project.assetDirectory.c_str(), projectFilePath);
        ScanAssets();
        // 프로젝트 기본 샘플러는 로드 때 적용되므로 잇기 전에 정한다(D-117).
        m_assets->SetDefaultTextureFilter(project.textureFilter);
        m_assets->Bind(*m_platform, m_assetRegistry, m_assetRoot.c_str());
        if (m_watchAssetDirectory)
        {
            // 감시가 서지 않아도(폴더 없음) 프로젝트는 열린다. 그때는 변경이 오지 않을 뿐이다.
            m_platform->WatchDirectory(m_assetRoot.c_str());
        }
        return true;
    }

    bool EngineInstance::ScanAssets()
    {
        AssetScanOptions scanOptions;
        scanOptions.ignorePatterns.data = m_project.assetIgnorePatterns.Data();
        scanOptions.ignorePatterns.size = static_cast<std::uint32_t>(m_project.assetIgnorePatterns.Size());
        scanOptions.createMissingMeta = m_createMissingAssetMeta;
        if (false == m_assetRegistry.Scan(*m_platform, m_assetRoot.c_str(), scanOptions, m_assetScanReport))
        {
            m_assetRegistry.Clear();
            m_assetScanReport = {};
            return false;
        }
        return true;
    }

    EngineInstance::AssetChangeSummary EngineInstance::PollAssetChanges()
    {
        AssetChangeSummary summary;
        if (m_platform == nullptr || m_assets.Get() == nullptr || false == m_assets->IsBound())
        {
            return summary;
        }
        // 한 레코드 경로의 아이디들(이미지는 Texture 와 Sprite 둘)이다.
        const auto recordsAt = [&](std::string_view path, AssetId (&ids)[2]) -> std::uint32_t {
            const AssetRecord* primary = m_assetRegistry.FindByPath(path);
            if (primary == nullptr)
            {
                return 0;
            }
            ids[0] = primary->id;
            std::uint32_t count = 1;
            for (std::size_t index = 0; index < m_assetRegistry.GetCount() && count < 2; ++index)
            {
                const AssetRecord& record = m_assetRegistry.GetRecord(index);
                if (record.owner == primary->id && record.id != primary->id)
                {
                    ids[count++] = record.id;
                }
            }
            return count;
        };
        bool rescan = false;
        FileEvent events[64];
        for (;;)
        {
            const std::uint32_t taken = m_platform->TakeFileEvents(events, 64);
            if (taken == 0)
            {
                break;
            }
            for (std::uint32_t at = 0; at < taken; ++at)
            {
                const FileEvent& event = events[at];
                if (event.kind == FileEventKind::Overflow)
                {
                    rescan = true;
                    continue;
                }
                // 메타의 변경은 우리가 쓴 것이거나 사용자가 손으로 고친 것이다. 전자는 이미 적용됐고, 후자는 다음
                // 로드가 본다 - 자기 반향으로 재로드를 돌리지 않는다(D-117).
                if (AssetTypeRules::IsMetaPath(event.path)
                    || (event.kind == FileEventKind::Renamed && AssetTypeRules::IsMetaPath(event.oldPath)))
                {
                    continue;
                }
                AssetId ids[2];
                switch (event.kind)
                {
                case FileEventKind::Created:
                    rescan = true;
                    break;
                case FileEventKind::Modified:
                {
                    const std::uint32_t count = recordsAt(event.path, ids);
                    for (std::uint32_t index = 0; index < count; ++index)
                    {
                        if (m_assets->ReloadInPlace(ids[index]))
                        {
                            ++summary.reloaded;
                        }
                    }
                    break;
                }
                case FileEventKind::Removed:
                {
                    const std::uint32_t count = recordsAt(event.path, ids);
                    if (count == 0)
                    {
                        // 폴더였거나 모르는 파일이다. 폴더면 그 아래가 통째로 갔다.
                        rescan = true;
                        break;
                    }
                    for (std::uint32_t index = 0; index < count; ++index)
                    {
                        m_assetRegistry.Unregister(ids[index]);
                    }
                    ++summary.removed;
                    break;
                }
                case FileEventKind::Renamed:
                    if (m_assetRegistry.Rename(event.oldPath, event.path))
                    {
                        ++summary.renamed;
                    }
                    else
                    {
                        rescan = true;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        if (rescan)
        {
            ScanAssets();
            summary.rescanned = true;
        }
        return summary;
    }

    const AssetRegistry& EngineInstance::GetAssetRegistry() const
    {
        return m_assetRegistry;
    }

    const AssetScanReport& EngineInstance::GetAssetScanReport() const
    {
        return m_assetScanReport;
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
        m_scriptModuleLoaded = false;
        m_scriptModuleError.clear();
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
                        // **못 실어도 프로젝트는 연다**(D-98). 여기서 막으면 아직 한 번도
                        // 빌드하지 않은 프로젝트를 열 길이 없어진다 - 스크립트를 쓰려면
                        // 에디터에서 빌드해야 하는데 그 에디터가 열리지 않는다.
                        m_scriptModuleLoaded = m_scripts.Load(
                            scriptModulePath, *m_platform, blocks.data, blocks.size);
                        if (false == m_scriptModuleLoaded)
                        {
                            m_scriptModuleError = "the script module could not be loaded: ";
                            m_scriptModuleError.append(scriptModulePath);
                            std::printf("note: %s\n", m_scriptModuleError.c_str());
                        }
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
        // **프로젝트가 없어도 에디터는 그릴 것이 있다.** 오버레이가 걸려 있으면
        // 프레임을 그대로 연다 - 그러지 않으면 프로젝트를 닫아 둔 에디터가
        // 검은 창이 된다. 최소화와 프로젝트 정리 중은 여전히 건너뛴다:
        // 그릴 표면이 없거나, 지금 내려가는 중이다.
        const bool nothingToDraw =
            m_framework == nullptr && false == m_renderer->HasFrameOverlay();
        if (nothingToDraw || m_projectCloseRequested
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
        const auto beginStatus = m_renderer->BeginFrame(m_gameViewTarget);
        m_lastFrameStatus = beginStatus;
        if (beginStatus == FrameStatus::Skipped)
        {
            return true;
        }
        if (beginStatus != FrameStatus::Ready)
        {
            return false;
        }
        // 프로젝트가 없으면 게임이 제출할 것도 없다. 그 프레임은 오버레이가 산다.
        const RenderResult renderResult = m_framework != nullptr
            ? m_framework->Render()
            : RenderResult::NothingToSubmit;
        if (renderResult == RenderResult::Failed || m_exitRequested)
        {
            m_lastFrameStatus = renderResult == RenderResult::Failed
                ? FrameStatus::InvalidState
                : FrameStatus::Ready;
            m_renderer->AbortFrame();
            return false;
        }
        // 그릴 것이 없는 프레임은 버리되, 루프는 살아있다(F-7).
        //
        // **오버레이가 걸려 있으면 버리지 않는다.** 에디터에서는 게임 화면이
        // 텍스처로 가서 백버퍼에 낼 것이 없는 것이 정상이고, 그 프레임을 버리면
        // 에디터 UI 까지 같이 사라진다 - 화면이 통째로 멈춘 것처럼 보인다.
        const bool nothingToShow = renderResult == RenderResult::NothingToSubmit
            && false == m_renderer->HasFrameOverlay();
        if (nothingToShow || m_projectCloseRequested)
        {
            m_renderer->AbortFrame();
            m_lastFrameStatus = FrameStatus::Skipped;
            return true;
        }
        const auto endStatus = m_renderer->EndFrame();
        m_lastFrameStatus = endStatus;
        return endStatus == FrameStatus::Ready || endStatus == FrameStatus::Skipped;
    }

    bool EngineInstance::SetGameViewTarget(const FrameTarget& target)
    {
        if (m_state == State::Ticking)
        {
            return false;
        }
        m_gameViewTarget = target;
        return true;
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
        if (m_platform != nullptr)
        {
            m_platform->StopWatching();
        }
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
        m_scriptModuleLoaded = false;
        m_scriptModuleError.clear();
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

    bool EngineInstance::IsScriptModuleLoaded() const
    {
        return m_scriptModuleLoaded;
    }

    const String& EngineInstance::GetScriptModuleError() const
    {
        return m_scriptModuleError;
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
