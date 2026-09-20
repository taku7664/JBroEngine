#include <JBro/Core/Log.h>
#include <JBro/Core/Profiler.h>
#include <JBro/Host/EngineInstance.h>

#include <JBro/Network/Internal/ScriptModuleContext.h>
#include <JBro/Network/SteadyClock.h>
#include <JBro/NetworkSystem/NetworkHost.h>

#include <JBro/Asset/AssetTypeRules.h>

#include <cmath>
#include <iterator>
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
            // 네트워크(D-122). 소켓은 플랫폼이 내어 주고, 없는 플랫폼이면 null 인 채로 선다 - 그때 모든 연결 시도는 거짓이다.
            if (config.networkEnabled)
            {
                m_socketProvider = platform.CreateSocketProvider();
                m_networkClock = MakeOwnerPtr<Network::SteadyClock>();
                m_network = MakeOwnerPtr<NetworkHost>(m_socketProvider.Get(), *m_networkClock);
                BindNetworkSystemContext(m_network->GetSystemContext());
                BindNetworkServiceContext(m_network->GetServiceContext());
            }
            m_frameworkContext.network = m_network.Get();
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
        ScanAssets(true);
        m_pendingReloads.Clear();
        m_assetRescanPending = false;
        m_assetOverflowPending = false;
        m_assetQuietFrames = 0;
        // 프로젝트 기본 샘플러는 로드 때 적용되므로 잇기 전에 정한다(D-117).
        m_assets->SetDefaultTextureFilter(project.textureFilter);
        m_assets->Bind(*m_platform, m_assetRegistry, m_assetRoot.c_str());
        if (m_watchAssetDirectory && false == m_platform->WatchDirectory(m_assetRoot.c_str()))
        {
            // 감시가 서지 않아도(폴더 없음) 프로젝트는 열린다. 그때는 변경이 오지 않을 뿐이다 - `IsWatchingAssets` 가 말한다.
            Log::Write(LogLevel::Info, "asset", "the asset folder is not watched: %s",
                m_assetRoot.c_str());
        }
        return true;
    }

    bool EngineInstance::ScanAssets(bool initial)
    {
        AssetScanOptions scanOptions;
        scanOptions.ignorePatterns.data = m_project.assetIgnorePatterns.Data();
        scanOptions.ignorePatterns.size = static_cast<std::uint32_t>(m_project.assetIgnorePatterns.Size());
        scanOptions.createMissingMeta = m_createMissingAssetMeta;
        if (initial)
        {
            if (false == m_assetRegistry.Scan(*m_platform, m_assetRoot.c_str(), scanOptions, m_assetScanReport))
            {
                m_assetRegistry.Clear();
                m_assetScanReport = {};
                return false;
            }
            return true;
        }
        // 다시 스캔은 새 표에 하고 성공했을 때만 바꿔 끼운다. 폴더가 잠깐 잠기거나 옮겨진 한 프레임의 실패로 모든 에셋
        // 참조가 풀리면 안 된다. 에셋 시스템은 이 객체의 주소를 들고 있으므로 옮겨 넣어야지 다른 객체를 가리키게 하면 안 된다.
        AssetRegistry fresh;
        AssetScanReport report;
        if (false == fresh.Scan(*m_platform, m_assetRoot.c_str(), scanOptions, report))
        {
            return false;
        }
        m_assetRegistry = std::move(fresh);
        m_assetScanReport = report;
        return true;
    }

    bool EngineInstance::RescanAssets()
    {
        if (m_platform == nullptr || m_assets.Get() == nullptr || false == m_assets->IsBound())
        {
            return false;
        }
        return ScanAssets(false);
    }

    bool EngineInstance::IsWatchingAssets() const
    {
        return m_platform != nullptr && m_platform->IsWatching();
    }

    void EngineInstance::QueueReload(AssetId id)
    {
        for (std::size_t index = 0; index < m_pendingReloads.Size(); ++index)
        {
            if (m_pendingReloads[index].id == id)
            {
                return;
            }
        }
        PendingReload pending;
        pending.id = id;
        m_pendingReloads.Add(pending);
    }

    void EngineInstance::HandleAssetEvent(const FileEvent& event, AssetChangeSummary& summary)
    {
        if (event.kind == FileEventKind::Overflow)
        {
            m_assetRescanPending = true;
            m_assetOverflowPending = true;
            return;
        }
        // 메타의 변경은 우리가 쓴 것이거나 사용자가 손으로 고친 것이다. 전자는 이미 적용됐고, 후자는 다음 로드가
        // 본다 - 자기 반향으로 재로드를 돌리지 않는다(D-117). 이름 바꾸기는 **양쪽이 다 메타일 때만** 건너뛴다 -
        // 한쪽만 메타면 에셋 파일이 생기거나 없어진 것이라 다시 본다.
        const bool pathIsMeta = AssetTypeRules::IsMetaPath(event.path) || AssetTypeRules::IsMetaScratchPath(event.path);
        if (event.kind == FileEventKind::Renamed)
        {
            const bool oldIsMeta = AssetTypeRules::IsMetaPath(event.oldPath)
                || AssetTypeRules::IsMetaScratchPath(event.oldPath);
            if (pathIsMeta && oldIsMeta)
            {
                return;
            }
            if (pathIsMeta != oldIsMeta)
            {
                m_assetRescanPending = true;
                return;
            }
        }
        else if (pathIsMeta)
        {
            return;
        }
        // 한 레코드 경로의 아이디들: 경로의 주인과 그것을 가리키는 것들(이미지의 Sprite). 색인이라 걷지 않는다.
        const auto recordsAt = [&](std::string_view path, Array<AssetId>& ids) {
            ids.Clear();
            const AssetRecord* primary = m_assetRegistry.FindByPath(path);
            if (primary == nullptr)
            {
                return;
            }
            ids.Add(primary->id);
            m_assetRegistry.CollectOwned(primary->id, ids);
        };
        Array<AssetId> ids;
        switch (event.kind)
        {
        case FileEventKind::Created:
            m_assetRescanPending = true;
            break;
        case FileEventKind::Modified:
        {
            recordsAt(event.path, ids);
            for (std::size_t index = 0; index < ids.Size(); ++index)
            {
                QueueReload(ids[index]);
            }
            break;
        }
        case FileEventKind::Removed:
        {
            recordsAt(event.path, ids);
            if (ids.IsEmpty())
            {
                // 폴더였거나 모르는 파일이다. 폴더면 그 아래가 통째로 갔다.
                m_assetRescanPending = true;
                break;
            }
            // 주인을 빼면 가리키던 것도 같이 빠진다.
            m_assetRegistry.Unregister(ids[0]);
            ++summary.removed;
            break;
        }
        case FileEventKind::Renamed:
        {
            // **메타를 같이 옮긴다.** 파일만 옮기면 다음 스캔이 메타 없는 파일에 새 아이디를 만들어 캔버스의 참조가 끊긴다.
            const AssetRecord* record = m_assetRegistry.FindByPath(event.oldPath);
            if (record == nullptr)
            {
                m_assetRescanPending = true;
                break;
            }
            String newSource = m_assets->GetAssetRoot();
            newSource.push_back('/');
            newSource.append(event.path);
            const bool carried = m_platform->MoveFileTo(m_assets->GetMetaPath(*record).c_str(),
                AssetTypeRules::MakeMetaPath(newSource).c_str());
            if (carried && m_assetRegistry.Rename(event.oldPath, event.path))
            {
                ++summary.renamed;
            }
            else
            {
                m_assetRescanPending = true;
            }
            break;
        }
        default:
            break;
        }
    }

    void EngineInstance::ApplyPendingAssetChanges(AssetChangeSummary& summary)
    {
        if (m_assetRescanPending)
        {
            if (ScanAssets(false))
            {
                summary.rescanned = true;
            }
            else
            {
                summary.rescanFailed = true;
            }
            m_assetRescanPending = false;
        }
        if (m_assetOverflowPending)
        {
            // 무엇이 바뀌었는지 모른다. 로드된 것을 전부 다시 읽는다.
            summary.reloaded += m_assets->ReloadAllInPlace();
            m_assetOverflowPending = false;
            m_pendingReloads.Clear();
            return;
        }
        std::size_t kept = 0;
        for (std::size_t index = 0; index < m_pendingReloads.Size(); ++index)
        {
            PendingReload pending = m_pendingReloads[index];
            const bool loaded = m_assets->IsLoaded(m_assets->Find(pending.id));
            if (false == loaded)
            {
                continue;
            }
            if (m_assets->ReloadInPlace(pending.id))
            {
                ++summary.reloaded;
                continue;
            }
            // 아직 쓰는 중이었을 수 있다. 다음 조용한 때 다시 해 본다.
            if (++pending.attempts < AssetReloadAttempts)
            {
                m_pendingReloads[kept++] = pending;
            }
        }
        m_pendingReloads.Resize(kept);
    }

    EngineInstance::AssetChangeSummary EngineInstance::PollAssetChanges()
    {
        AssetChangeSummary summary;
        if (m_platform == nullptr || m_assets.Get() == nullptr || false == m_assets->IsBound())
        {
            return summary;
        }
        bool any = false;
        for (;;)
        {
            const std::uint32_t taken = m_platform->TakeFileEvents(
                m_fileEvents, static_cast<std::uint32_t>(std::size(m_fileEvents)));
            if (taken == 0)
            {
                break;
            }
            any = true;
            for (std::uint32_t at = 0; at < taken; ++at)
            {
                HandleAssetEvent(m_fileEvents[at], summary);
            }
        }
        // 조용해진 뒤에 적용한다 - 저장이 끝나기 전의 알림과 대량 복사의 한 파일마다를 하나로 묶는다.
        m_assetQuietFrames = any ? 0 : m_assetQuietFrames + 1;
        const bool pending = m_assetRescanPending || m_assetOverflowPending || false == m_pendingReloads.IsEmpty();
        if (pending && m_assetQuietFrames >= AssetQuietFramesBeforeApply)
        {
            ApplyPendingAssetChanges(summary);
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
                // 지금 정해져 있는 값을 새 프레임워크에도 먹인다(D-131). 에디터가 멈춘 채로
                // 다음 프로젝트를 열면 그 프로젝트도 멈춘 채로 떠야 한다.
                if (initialized)
                {
                    framework.SetSimulationEnabled(m_simulationEnabled);
                }
                if (initialized && false == m_projectCloseRequested && false == m_exitRequested)
                {
                    m_scriptContextsBound = framework.BindScriptContexts();
                    initialized = m_scriptContextsBound;
                    // 컨텍스트가 붙은 뒤에 싣는다. 로더가 그 컨텍스트를 읽어 DLL 에 넘긴다.
                    if (initialized && scriptModulePath != nullptr && scriptModulePath[0] != 0)
                    {
                        const JArrayView<ScriptContextBlock> frameworkBlocks =
                            framework.GetScriptContextBlocks();
                        // 프레임워크의 블록 뒤에 호스트의 네트워크 블록을 잇는다(D-122). 네트워크는 호스트 것이고
                        // 두 차원이 같은 것을 쓰므로 프레임워크가 아니라 여기서 낸다.
                        Array<ScriptContextBlock> blocks;
                        blocks.Reserve(frameworkBlocks.size + 2);
                        blocks.Append(frameworkBlocks.data, frameworkBlocks.size);
                        if (m_network)
                        {
                            blocks.Add(MakeNetworkSystemContextBlock(m_network->GetSystemContext()));
                            blocks.Add(MakeNetworkServiceContextBlock(m_network->GetServiceContext()));
                        }
                        // **못 실어도 프로젝트는 연다**(D-98). 여기서 막으면 아직 한 번도
                        // 빌드하지 않은 프로젝트를 열 길이 없어진다 - 스크립트를 쓰려면
                        // 에디터에서 빌드해야 하는데 그 에디터가 열리지 않는다.
                        m_scriptModuleLoaded = m_scripts.Load(
                            scriptModulePath, *m_platform, blocks.Data(), static_cast<std::uint32_t>(blocks.Size()));
                        if (false == m_scriptModuleLoaded)
                        {
                            m_scriptModuleError = "the script module could not be loaded: ";
                            m_scriptModuleError.append(scriptModulePath);
                            Log::Write(LogLevel::Info, "script", "%s", m_scriptModuleError.c_str());
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
        // 프레임의 끝이다. 여기서 이번 프레임의 구간을 읽을 수 있는 자리로 옮긴다.
        Profiler::EndFrame();
        if (m_projectCloseRequested)
        {
            CloseProject();
        }
        return m_state == State::Running;
    }

    bool EngineInstance::TickFrame(float deltaTime)
    {
        // 프레임의 구간을 나눠 잰다(D-138). 꺼져 있으면 이 줄들은 값이 없는 호출이다.
        Profiler::BeginFrame();
        const ProfileScope frameScope("Frame");
        {
            const ProfileScope scope("Platform");
            m_platform->PumpEvents();
        }
        if (m_exitRequested || m_platform->ShouldClose(m_mainWindow))
        {
            return false;
        }
        // 소켓은 프레임 밖에서 돌린다. 이 뒤의 고정 스텝이 받은 것을 입히고 보낼 것을 만든다.
        if (m_network)
        {
            const ProfileScope scope("Network");
            m_network->Update();
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
        // **멈춰 있어도 갱신은 돈다**(D-131). 세우는 것은 스크립트·물리이고, 트랜스폼과
        // 렌더 추출은 그대로 돌아야 한다 - 그리는 것은 추출한 목록에서 나오므로 그것까지
        // 세우면 편집 화면이 빈 화면이 된다. 무엇을 세울지는 프레임워크가 안다.
        if (m_framework != nullptr && false == m_projectCloseRequested)
        {
            const ProfileScope scope("Update");
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
        RenderResult renderResult = RenderResult::NothingToSubmit;
        if (m_framework != nullptr)
        {
            const ProfileScope scope("Submit");
            renderResult = m_framework->Render();
        }
        // **편집 화면은 게임 화면 바로 뒤다**(D-130). 프레임워크가 이번 프레임에 모아 둔
        // 그릴 것을 그대로 쓰므로, `Render` 와 같은 프레임 안에서만 뜻이 있다.
        // 요청은 한 프레임짜리라 여기서 비운다.
        if (m_hasEditorView)
        {
            const EditorViewDesc requested = m_editorView;
            m_hasEditorView = false;
            m_editorView = {};
            if (m_framework != nullptr && renderResult != RenderResult::Failed)
            {
                const RenderResult editorResult = m_framework->RenderEditorView(requested);
                if (editorResult == RenderResult::Failed)
                {
                    renderResult = RenderResult::Failed;
                }
                else if (editorResult == RenderResult::Submitted)
                {
                    // 게임 카메라가 없어도 이 프레임에는 낼 것이 있다.
                    renderResult = RenderResult::Submitted;
                }
            }
        }
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
        FrameStatus endStatus = FrameStatus::Skipped;
        {
            // 여기에 그리기와 제시가 다 들어 있다. UI 오버레이도 이 안에서 불린다.
            const ProfileScope scope("Render");
            endStatus = m_renderer->EndFrame();
        }
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

    bool EngineInstance::RequestEditorView(const EditorViewDesc& view)
    {
        if (m_state == State::Ticking)
        {
            return false;
        }
        if (false == view.target.IsValid() || view.extent.width == 0 || view.extent.height == 0)
        {
            return false;
        }
        m_editorView = view;
        m_hasEditorView = true;
        return true;
    }

    void EngineInstance::SetProjectFile(const ProjectFile& project)
    {
        m_project = project;
        // 이것만 지금 적용된다. 로드할 때 쓰는 값이므로 다음 로드부터 새 값이다.
        if (m_assets.Get() != nullptr)
        {
            m_assets->SetDefaultTextureFilter(project.textureFilter);
        }
    }

    void EngineInstance::SetSimulationEnabled(bool enabled)
    {
        m_simulationEnabled = enabled;
        if (m_framework != nullptr)
        {
            m_framework->SetSimulationEnabled(enabled);
        }
    }

    bool EngineInstance::IsSimulationEnabled() const
    {
        return m_simulationEnabled;
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
        // 네트워크는 프로젝트 뒤, 플랫폼 앞에 내린다 - 소켓은 플랫폼의 것이다.
        if (m_network)
        {
            BindNetworkSystemContext({});
            BindNetworkServiceContext({});
            m_network.Reset();
        }
        m_socketProvider.Reset();
        m_networkClock.Reset();
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

    NetworkHost* EngineInstance::GetNetwork()
    {
        return m_network.Get();
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
