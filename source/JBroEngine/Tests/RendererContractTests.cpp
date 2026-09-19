#include <JBro/Graphics/Renderer.h>

#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Runtime/ComponentLookupStats.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
#if defined(_MSC_VER) && defined(_DEBUG)
    int frameAllocations = 0;
    int CountFrameAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++frameAllocations;
        }
        return 1;
    }

    class FrameAllocationProbe
    {
    public:
        FrameAllocationProbe() : m_previous(_CrtSetAllocHook(&CountFrameAllocations))
        {
            frameAllocations = 0;
        }
        ~FrameAllocationProbe()
        {
            _CrtSetAllocHook(m_previous);
        }
    private:
        _CRT_ALLOC_HOOK m_previous;
    };
#endif

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeCommandContext final : public JBro::IRHICommandContext
    {
    public:
        bool BeginRenderPass(const JBro::RenderPassDesc& desc) override
        {
            ++beginRenderPassCount;
            // 뷰가 어디로 갔는지는 첨부 텍스처에만 남는다. 카운터만 세면
            // 백버퍼로 가든 게임 뷰 텍스처로 가든 같은 숫자다.
            if (desc.colorAttachments.data != nullptr && desc.colorAttachments.size != 0)
            {
                lastColorAttachment = desc.colorAttachments.data[0].texture;
            }
            return true;
        }

        void EndRenderPass() override
        {
            ++endRenderPassCount;
        }

        void SetViewport(const JBro::Viewport& value) override
        {
            viewport = value;
        }

        void SetScissor(const JBro::ScissorRect&) override
        {
        }

        bool SetGraphicsPipeline(JBro::GraphicsPipelineHandle) override
        {
            ++setPipelineCount;
            return true;
        }

        bool SetVertexBuffer(
            std::uint32_t,
            JBro::BufferHandle,
            std::uint32_t,
            std::size_t) override
        {
            ++setVertexBufferCount;
            return true;
        }

        bool SetIndexBuffer(JBro::BufferHandle, JBro::IndexFormat, std::size_t) override
        {
            ++setIndexBufferCount;
            return true;
        }

        bool SetGraphicsConstants(JBro::JArrayView<std::byte> data) override
        {
            if (data.size == sizeof(viewProjection.values))
            {
                std::memcpy(viewProjection.values, data.data, data.size);
            }
            ++setGraphicsConstantsCount;
            return true;
        }

        // 이 가짜는 텍스처를 쓰지 않는다. 받아 두기만 하고 무엇도 그리지 않는다 —
        // 스프라이트 경로는 아직 텍스처를 묶지 않으므로 세어 둘 값도 없다.
        bool SetTexture(std::uint32_t, JBro::TextureHandle) override
        {
            return true;
        }

        bool SetSampler(std::uint32_t, JBro::SamplerHandle) override
        {
            return true;
        }

        bool DrawIndexedInstanced(
            std::uint32_t,
            std::uint32_t instanceCount,
            std::uint32_t,
            std::int32_t,
            std::uint32_t firstInstance) override
        {
            if (drawIndexedInstancedCount < MaxRecordedDraws)
            {
                draws[drawIndexedInstancedCount] = {instanceCount, firstInstance};
            }
            ++drawIndexedInstancedCount;
            return true;
        }

        struct Draw
        {
            std::uint32_t instanceCount = 0;
            std::uint32_t firstInstance = 0;
        };
        static constexpr std::uint32_t MaxRecordedDraws = 8;
        Draw draws[MaxRecordedDraws] = {};

        std::uint32_t beginRenderPassCount = 0;
        JBro::TextureHandle lastColorAttachment;
        std::uint32_t endRenderPassCount = 0;
        std::uint32_t setPipelineCount = 0;
        std::uint32_t setVertexBufferCount = 0;
        std::uint32_t setIndexBufferCount = 0;
        std::uint32_t setGraphicsConstantsCount = 0;
        std::uint32_t drawIndexedInstancedCount = 0;
        JBro::Viewport viewport;
        JBro::Matrix4x4 viewProjection;
    };

    class FakeDevice final : public JBro::IRHIDevice
    {
    public:
        JBro::BufferHandle CreateBuffer(const JBro::BufferDesc&) override
        {
            return {1, 1};
        }

        void DestroyBuffer(JBro::BufferHandle) override
        {
        }

        bool WriteBuffer(
            JBro::BufferHandle,
            std::size_t,
            JBro::JArrayView<std::byte> data) override
        {
            // 60 = 아핀 6 + 깊이 1 + 틴트 4 + UV 사각형 4. 셰이더와 공유하는 스트라이드다(D-113).
            constexpr std::size_t Stride = sizeof(JBro::SpriteTransform2D) + 16 + 16;
            if (data.size >= Stride && data.size % Stride == 0)
            {
                uploadedInstanceCount = data.size / Stride;
                std::memcpy(&firstInstanceWorld, data.data, sizeof(firstInstanceWorld));
                std::memcpy(firstInstanceTint, data.data + sizeof(JBro::SpriteTransform2D), sizeof(firstInstanceTint));
            }
            lastInstanceUploadBytes = data.size;
            ++writeBufferCount;
            return true;
        }

        JBro::TextureHandle CreateTexture(const JBro::TextureDesc&) override
        {
            return {1, 1};
        }

        void DestroyTexture(JBro::TextureHandle) override
        {
        }

        bool WriteTexture(
            JBro::TextureHandle,
            std::uint32_t,
            JBro::JArrayView<std::byte>) override
        {
            return true;
        }

        JBro::SamplerHandle CreateSampler(const JBro::SamplerDesc&) override
        {
            return {1, 1};
        }

        void DestroySampler(JBro::SamplerHandle) override
        {
        }

        JBro::GraphicsPipelineHandle CreateGraphicsPipeline(
            const JBro::GraphicsPipelineDesc&) override
        {
            ++createPipelineCount;
            return {1, 1};
        }

        void DestroyGraphicsPipeline(JBro::GraphicsPipelineHandle) override
        {
            ++destroyPipelineCount;
        }

        JBro::SwapchainHandle CreateSwapchain(const JBro::SwapchainDesc& desc) override
        {
            swapchainDesc = desc;
            ++createSwapchainCount;
            return {1, 1};
        }

        void DestroySwapchain(JBro::SwapchainHandle) override
        {
            ++destroySwapchainCount;
        }

        bool ResizeSwapchain(JBro::SwapchainHandle, const JBro::Extent2D& extent) override
        {
            resizedExtent = extent;
            ++resizeSwapchainCount;
            return resizeSucceeds;
        }

        JBro::BeginFrameResult BeginFrame(JBro::SwapchainHandle) override
        {
            ++beginFrameCount;

            JBro::BeginFrameResult result;
            result.status = beginStatus;
            result.frame.serial = beginFrameCount;
            result.frame.slot = 0;
            result.frame.backBuffer = {2, 1};
            result.frame.commands = &commands;
            return result;
        }

        JBro::FrameStatus EndFrame(const JBro::FrameContext&) override
        {
            ++endFrameCount;
            return endStatus;
        }

        void AbortFrame(const JBro::FrameContext&) override
        {
            ++abortFrameCount;
        }

        JBro::FrameStatus GetStatus() const override
        {
            return deviceStatus;
        }

        void WaitIdle() override
        {
            ++waitIdleCount;
        }

        FakeCommandContext commands;
        bool resizeSucceeds = true;
        JBro::FrameStatus beginStatus = JBro::FrameStatus::Ready;
        JBro::FrameStatus endStatus = JBro::FrameStatus::Ready;
        JBro::FrameStatus deviceStatus = JBro::FrameStatus::Ready;
        JBro::SwapchainDesc swapchainDesc;
        JBro::Extent2D resizedExtent;
        std::uint32_t createSwapchainCount = 0;
        std::uint32_t destroySwapchainCount = 0;
        std::uint32_t resizeSwapchainCount = 0;
        std::uint32_t beginFrameCount = 0;
        std::uint32_t endFrameCount = 0;
        std::uint32_t abortFrameCount = 0;
        std::uint32_t waitIdleCount = 0;
        std::uint32_t writeBufferCount = 0;
        std::uint32_t createPipelineCount = 0;
        std::uint32_t destroyPipelineCount = 0;
        std::uint32_t uploadedInstanceCount = 0;
        std::size_t lastInstanceUploadBytes = 0;
        JBro::SpriteTransform2D firstInstanceWorld;
        float firstInstanceTint[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    class FakeModule final : public JBro::IRHIModule
    {
    public:
        bool Initialize(const JBro::JMemoryContext&) override
        {
            return true;
        }

        void Shutdown() override
        {
        }

        JBro::GraphicsApi GetApi() const override
        {
            return JBro::GraphicsApi::D3D12;
        }

        JBro::IRHIDevice* CreateDevice(const JBro::RHIDeviceCreateInfo& createInfo) override
        {
            lastCreateInfo = createInfo;
            ++createDeviceCount;
            return &device;
        }

        void DestroyDevice(JBro::IRHIDevice* value) override
        {
            Check(value == &device, "renderer must destroy the device it created");
            ++destroyDeviceCount;
        }

        FakeDevice device;
        JBro::RHIDeviceCreateInfo lastCreateInfo;
        std::uint32_t createDeviceCount = 0;
        std::uint32_t destroyDeviceCount = 0;
    };

    class HostPlatform final : public JBro::IPlatform
    {
    public:
        bool Initialize(const JBro::JMemoryContext&) override
        {
            return true;
        }
        void Shutdown() override
        {
            Check(false, "engine must not shut down the borrowed platform module");
        }
        JBro::WindowHandle OpenPlatformWindow(const JBro::WindowDesc&) override
        {
            open = true;
            closeRequested = false;
            return {1};
        }
        void ClosePlatformWindow(JBro::WindowHandle) override
        {
            Check(module->destroyDeviceCount == module->createDeviceCount,
                "device must be destroyed before its native surface");
            Check(module->device.waitIdleCount == module->destroyDeviceCount,
                "shutdown must drain GPU work before closing the window");
            open = false;
            ++closeCount;
        }
        JBro::SurfaceHandle CreateSurface(JBro::WindowHandle window) override
        {
            return {window.value};
        }
        void PumpEvents() override
        {
            ++pumpCount;
        }

        JBro::JArrayView<JBro::InputEvent> GetInputEvents() const override
        {
            return {};
        }
        void WaitForEvents(std::uint32_t) override
        {
        }
        bool ShouldClose(JBro::WindowHandle) const override
        {
            return closeRequested;
        }
        bool GetWindowState(JBro::WindowHandle, JBro::WindowState& result) const override
        {
            result = state;
            return open;
        }
        JBro::DynamicLibrary LoadDynamicLibrary(const char*) override
        {
            return {};
        }
        void* GetSymbol(JBro::DynamicLibrary, const char*) override
        {
            return nullptr;
        }
        void UnloadDynamicLibrary(JBro::DynamicLibrary) override
        {
        }
        FakeModule* module = nullptr;
        JBro::WindowState state{320, 180, false};
        bool open = false;
        bool closeRequested = false;
        int pumpCount = 0;
        int closeCount = 0;
    };

    class HostFramework final : public JBro::IFramework
    {
    public:
        bool Initialize(const JBro::FrameworkContext& value) override
        {
            context = value;
            Check(value.renderer != nullptr && value.renderer->IsInitialized(), "GPU must precede framework init");
            Check(value.assets != nullptr, "framework must receive the existing asset service");
            if (closeDuringInitialize)
            {
                engine->CloseProject();
            }
            if (exitDuringInitialize)
            {
                engine->Shutdown();
            }
            if (throwDuringInitialize)
            {
                throw std::runtime_error("expected project initialization failure");
            }
            return initializeSucceeds;
        }
        bool BindScriptContexts() noexcept override
        {
            ++contextBinds;
            contextsBound = bindContextsSucceeds;
            shutdownsAtBind = shutdowns;
            return bindContextsSucceeds;
        }
        void UnbindScriptContexts() noexcept override
        {
            Check(contextsBound, "script contexts must only unbind after a successful bind");
            Check(shutdowns == shutdownsAtBind,
                "script contexts must unbind before their framework shutdown");
            contextsBound = false;
            ++contextUnbinds;
        }
        void Update(float) override
        {
            ++updates;
            if (closeDuringUpdate)
            {
                engine->CloseProject();
                Check(shutdowns == shutdownsBeforeUpdate, "project cannot be destroyed on its own callback stack");
            }
            if (exitDuringUpdate)
            {
                engine->RequestExit();
            }
            if (throwDuringUpdate)
            {
                throw std::runtime_error("expected update failure");
            }
        }
        JBro::RenderResult Render() override
        {
            ++renders;
            // 뷰를 하나라도 열어야 렌더러가 패스를 기록한다. 그래야 "어디에 그렸는가" 가
            // 남는다 - 아무것도 제출하지 않으면 기록할 것도 없다.
            if (submitView && context.renderer != nullptr)
            {
                JBro::CameraParams camera;
                context.renderer->BeginView(camera);
                context.renderer->EndView();
            }
            if (closeDuringRender)
            {
                engine->CloseProject();
            }
            return renderResult;
        }
        void Shutdown() override
        {
            Check(platform->open && context.renderer->IsInitialized(), "framework must release while window and GPU live");
            Check(false == contextsBound, "framework shutdown must not retain bound script contexts");
            ++shutdowns;
            context = {};
            if (exitDuringShutdown)
            {
                engine->Shutdown();
            }
        }
        HostPlatform* platform = nullptr;
        JBro::EngineInstance* engine = nullptr;
        JBro::FrameworkContext context;
        int updates = 0;
        int renders = 0;
        int shutdowns = 0;
        int contextBinds = 0;
        int contextUnbinds = 0;
        int shutdownsAtBind = 0;
        bool contextsBound = false;
        bool bindContextsSucceeds = true;
        bool initializeSucceeds = true;
        JBro::RenderResult renderResult = JBro::RenderResult::Submitted;
        bool submitView = false;
        bool exitDuringUpdate = false;
        bool throwDuringUpdate = false;
        bool throwDuringInitialize = false;
        bool closeDuringInitialize = false;
        bool exitDuringInitialize = false;
        bool closeDuringRender = false;
        bool exitDuringShutdown = false;
        bool closeDuringUpdate = false;
        int shutdownsBeforeUpdate = 0;
    };

    // 오버레이가 불렸는지, 그때 백버퍼가 무엇이었는지 남긴다.
    void TestProjectSwitchPreservesProcessResources()
    {
        FakeModule module;
        HostPlatform platform;
        platform.module = &module;
        HostFramework first;
        HostFramework second;
        first.platform = &platform;
        second.platform = &platform;
        JBro::EngineInstance engine;
        first.engine = &engine;
        second.engine = &engine;
        JBro::EngineConfig config;
        Check(engine.Initialize(config, platform, module), "process initialization must not require a project");
        auto* processRenderer = engine.GetRenderer();
        Check(engine.Tick(0.016f) && engine.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "empty host must pump events without submitting undefined backbuffer content");
        Check(engine.GetAssetSystem() == nullptr && engine.OpenProject(first), "assets must be scoped to an opened project");
        Check(false == engine.OpenProject(second) && engine.GetFramework() == &first,
            "opening over a live project must reject without destroying it");
        Check(engine.Tick(0.016f), "first project must render");
        engine.CloseProject();
        Check(engine.IsRunning() && platform.open && engine.GetRenderer() == processRenderer,
            "project close must preserve the host, window and renderer");
        Check(first.shutdowns == 1 && engine.GetFramework() == nullptr && engine.GetAssetSystem() == nullptr,
            "project close must release framework session and assets");
        Check(module.createDeviceCount == 1 && module.destroyDeviceCount == 0
            && module.device.destroySwapchainCount == 0 && module.device.waitIdleCount == 0,
            "project close must not drain or recreate process-owned builtin GPU resources");
        engine.CloseProject();
        Check(first.shutdowns == 1 && engine.Tick(0.016f), "closed project must remain idempotently empty");
        Check(engine.OpenProject(second) && engine.Tick(0.016f), "next project must use the live process");
        Check(second.context.renderer == processRenderer && module.createDeviceCount == 1,
            "project switch must not create another device");
        second.closeDuringUpdate = true;
        second.shutdownsBeforeUpdate = second.shutdowns;
        const auto previousRenders = second.renders;
        Check(engine.Tick(0.016f) && second.renders == previousRenders && second.shutdowns == 1,
            "callback close must finish after update without rendering the closing project");
        Check(engine.IsRunning() && platform.open, "callback project close must not terminate the host");
        second.closeDuringUpdate = false;
        second.initializeSucceeds = false;
        Check(false == engine.OpenProject(second), "failed project open must be reported");
        Check(engine.IsRunning() && engine.GetFramework() == nullptr && engine.GetAssetSystem() == nullptr,
            "failed project open must roll back only project resources");
        second.initializeSucceeds = true;
        second.bindContextsSucceeds = false;
        const auto bindsBeforeFailure = second.contextBinds;
        const auto unbindsBeforeFailure = second.contextUnbinds;
        const auto shutdownsBeforeBindFailure = second.shutdowns;
        Check(false == engine.OpenProject(second), "script context binding failure must reject the project");
        Check(second.contextBinds == bindsBeforeFailure + 1
            && second.contextUnbinds == unbindsBeforeFailure
            && second.shutdowns == shutdownsBeforeBindFailure + 1
            && false == second.contextsBound,
            "failed script context binding must roll back without an unmatched unbind");
        second.bindContextsSucceeds = true;
        second.throwDuringInitialize = true;
        bool caught = false;
        try
        {
            engine.OpenProject(second);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        Check(caught && engine.IsRunning() && platform.open && module.destroyDeviceCount == 0,
            "project initialization exceptions must preserve reusable process resources");
        second.throwDuringInitialize = false;
        second.closeDuringInitialize = true;
        Check(false == engine.OpenProject(second) && engine.IsRunning() && engine.GetFramework() == nullptr,
            "close during initialize must cancel only the project");
        second.closeDuringInitialize = false;
        Check(engine.OpenProject(second), "project must reopen after an initialization exception");
        second.closeDuringRender = true;
        const auto beforeAbort = module.device.abortFrameCount;
        const auto beforePresent = module.device.endFrameCount;
        Check(engine.Tick(0.016f) && engine.GetFramework() == nullptr,
            "close during render must release the project after the callback");
        Check(module.device.abortFrameCount == beforeAbort + 1 && module.device.endFrameCount == beforePresent,
            "closing project's partial frame must abort without presenting");
        second.closeDuringRender = false;
        Check(engine.OpenProject(second), "project must reopen after render callback close");
        engine.RequestExit();
        Check(false == engine.Tick(0.016f), "process exit must stop and release everything");
        Check(module.destroyDeviceCount == 1 && platform.closeCount == 1 && module.device.waitIdleCount == 1,
            "only process exit must drain the GPU and destroy the window");
        Check(engine.Initialize(config, platform, module), "process must restart for shutdown-during-open test");
        second.exitDuringInitialize = true;
        Check(false == engine.OpenProject(second) && false == platform.open && false == engine.IsRunning(),
            "process shutdown during project initialization must finish after callback return");
        second.exitDuringInitialize = false;
        Check(engine.Initialize(config, platform, module) && engine.OpenProject(second), "process must restart for teardown callback test");
        second.exitDuringShutdown = true;
        engine.CloseProject();
        Check(false == platform.open && false == engine.IsRunning(),
            "process exit requested by project shutdown must not recurse into project teardown");
    }

    // Existing process-failure tests intentionally close the process if its first project fails.
    bool InitializeHost(JBro::EngineInstance& engine, const JBro::EngineConfig& config,
        HostPlatform& platform, FakeModule& module, HostFramework& framework)
    {
        if (false == engine.Initialize(config, platform, module))
        {
            return false;
        }
        if (false == engine.OpenProject(framework))
        {
            engine.Shutdown();
            return false;
        }
        return true;
    }

    void TestEngineHostLifecycle()
    {
        FakeModule module;
        HostPlatform platform;
        platform.module = &module;
        HostFramework framework;
        framework.platform = &platform;
        JBro::EngineInstance engine;
        framework.engine = &engine;
        JBro::EngineConfig config;
        config.window.visible = false;
        config.fixedDeltaTime = 0.02f;
        Check(InitializeHost(engine, config, platform, module, framework), "host must initialize");
        Check(framework.context.fixedDeltaTime == 0.02f, "host must forward fixed-step policy");
        Check(false == InitializeHost(engine, config, platform, module, framework), "double init must reject without teardown");
        for (int frame = 0; frame < 3; ++frame)
        {
#if defined(_MSC_VER) && defined(_DEBUG)
            FrameAllocationProbe probe;
#endif
            Check(engine.Tick(0.016f), "normal host tick must continue");
#if defined(_MSC_VER) && defined(_DEBUG)
            Check(frameAllocations == 0, "normal host path must not allocate");
#endif
        }
        Check(module.device.resizeSwapchainCount == 0 && module.device.waitIdleCount == 0,
            "unchanged surface must neither resize nor idle the GPU");
        platform.state = {640, 360, true};
        Check(engine.Tick(0.016f) && framework.updates == 4 && framework.renders == 3,
            "minimization must retain simulation and skip rendering");
        Check(module.device.beginFrameCount == 3 && module.device.resizeSwapchainCount == 0,
            "minimized host must not acquire or resize");
        platform.state = {0, 0, false};
        Check(engine.Tick(0.016f) && framework.updates == 5 && framework.renders == 3,
            "zero drawable area must also skip only rendering");
        platform.state = {800, 600, false};
        Check(engine.Tick(0.016f), "restored host must render");
        Check(module.device.resizeSwapchainCount == 1 && module.device.resizedExtent.width == 800,
            "restoration must resize once to the latest extent");
        Check(engine.Tick(0.016f) && module.device.resizeSwapchainCount == 1, "same extent must not resize again");
        module.device.beginStatus = JBro::FrameStatus::Skipped;
        const auto renderCount = framework.renders;
        Check(engine.Tick(0.016f) && framework.renders == renderCount, "unavailable backbuffer must skip submission");
        platform.closeRequested = true;
        const auto updateCount = framework.updates;
        Check(false == engine.Tick(0.016f) && framework.updates == updateCount, "close must stop before another update");
        Check(false == engine.IsRunning() && false == platform.open && framework.shutdowns == 1,
            "close must tear down the host");
        Check(engine.GetLastFrameStatus() == JBro::FrameStatus::Ready, "requested close must remain a normal exit");
        engine.Shutdown();
        Check(platform.closeCount == 1 && framework.shutdowns == 1, "shutdown must be idempotent");

        module.device.beginStatus = JBro::FrameStatus::Ready;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen after teardown");
        framework.renderResult = JBro::RenderResult::Failed;
        Check(false == engine.Tick(0.016f), "failed submission must terminate the host");
        Check(engine.GetLastFrameStatus() == JBro::FrameStatus::InvalidState, "submission failure must survive cleanup as an error");
        Check(module.device.abortFrameCount == 1, "failed submission must abort before teardown");
        framework.renderResult = JBro::RenderResult::Submitted;
        framework.initializeSucceeds = false;
        Check(false == InitializeHost(engine, config, platform, module, framework), "framework failure must roll back init");
        Check(false == platform.open && framework.shutdowns == 3, "partial framework init must also release in order");
        framework.initializeSucceeds = true;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen after init failure");

        // 그릴 것이 없는 프레임은 오류가 아니다(D-49/F-7).
        // 호스트가 프레임 아레나를 채우고 매 틱 되감는지 본다(D-52).
        JBro::LinearAllocator* frameMemory = engine.GetFrameMemory();
        Check(frameMemory != nullptr && frameMemory->IsInitialized(),
            "the host must supply a frame arena when the caller leaves memory.frame empty");
        JBro::JAllocator frameHandle = frameMemory->GetInterface();
        Check(frameHandle.allocate(frameHandle.userData, 4096, 16) != nullptr,
            "the frame arena must serve a request");
        Check(frameMemory->GetUsedBytes() >= 4096, "the request must advance the arena cursor");
        Check(engine.Tick(0.016f), "the host must tick after the arena was used");
        Check(frameMemory->GetUsedBytes() == 0, "every frame must rewind the arena before the framework runs");

        framework.renderResult = JBro::RenderResult::NothingToSubmit;
        const auto beforeEmptyRenders = framework.renders;
        const auto beforeEmptyAborts = module.device.abortFrameCount;
        const auto beforeEmptyEnds = module.device.endFrameCount;
        Check(engine.Tick(0.016f) && engine.IsRunning(), "a frame with nothing to submit must keep the host running");
        Check(engine.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "nothing to submit must report a skipped frame, not a failure");
        Check(framework.renders == beforeEmptyRenders + 1
            && module.device.abortFrameCount == beforeEmptyAborts + 1
            && module.device.endFrameCount == beforeEmptyEnds,
            "an empty frame must reach the framework and abort without presenting");
        Check(engine.Tick(0.016f) && engine.Tick(0.016f) && engine.IsRunning(),
            "the host must survive repeated empty frames");
        framework.renderResult = JBro::RenderResult::Submitted;

        framework.exitDuringUpdate = true;
        const auto beforeExitRender = framework.renders;
        Check(false == engine.Tick(0.016f) && framework.renders == beforeExitRender,
            "callback exit request must defer teardown until update returns and skip rendering");
        framework.exitDuringUpdate = false;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen after requested exit");
        framework.throwDuringUpdate = true;
        bool caught = false;
        try
        {
            engine.Tick(0.016f);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        Check(caught && false == platform.open && false == engine.IsRunning(), "callback exception must clean up before propagating");
        framework.throwDuringUpdate = false;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen for resize failure test");
        module.device.resizeSucceeds = false;
        platform.state.width = 900;
        const auto beforeResize = module.device.beginFrameCount;
        Check(false == engine.Tick(0.016f) && module.device.beginFrameCount == beforeResize && false == platform.open,
            "resize failure must stop before acquiring a backbuffer");
        module.device.resizeSucceeds = true;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen for device-loss test");
        module.device.deviceStatus = JBro::FrameStatus::DeviceLost;
        platform.state.minimized = true;
        const auto beforeLoss = framework.updates;
        Check(false == engine.Tick(0.016f) && framework.updates == beforeLoss && false == platform.open,
            "device loss is fatal even while minimized");
        Check(engine.GetLastFrameStatus() == JBro::FrameStatus::DeviceLost, "host must preserve device-loss reason");
        module.device.deviceStatus = JBro::FrameStatus::Ready;
        platform.state.minimized = false;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen for presentation failure test");
        module.device.endStatus = JBro::FrameStatus::SurfaceLost;
        Check(false == engine.Tick(0.016f) && false == platform.open, "presentation surface loss must shut down");
        Check(engine.GetLastFrameStatus() == JBro::FrameStatus::SurfaceLost, "host must preserve presentation failure reason");
        module.device.endStatus = JBro::FrameStatus::Ready;
        Check(InitializeHost(engine, config, platform, module, framework), "host must reopen for acquire failure test");
        module.device.beginStatus = JBro::FrameStatus::InvalidState;
        const auto beforeAcquire = framework.renders;
        Check(false == engine.Tick(0.016f) && framework.renders == beforeAcquire && false == platform.open,
            "failed acquisition must not call framework rendering");
        module.device.beginStatus = JBro::FrameStatus::Ready;
        const auto beforeInvalidConfig = module.createDeviceCount;
        config.fixedDeltaTime = 0.0f;
        Check(false == InitializeHost(engine, config, platform, module, framework)
            && module.createDeviceCount == beforeInvalidConfig, "invalid config must fail before native resource creation");
    }

    void TestFrameworkSubmitsTransformedBatches()
    {
        FakeModule module;
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.surface = {99};
        config.surfaceExtent = {200, 100};
        config.maxSpriteSubmissions = 70;
        Check(renderer.Initialize(module, config), "integration renderer must initialize");
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        context.renderer = &renderer;
        Check(framework.Initialize(context), "framework must bind a ready renderer");
        auto* canvas = framework.GetCanvas();
        Check(canvas->GetSystems().GetSystemCount() == 5, "canvas must own the five implemented default systems");
        auto* cameraObject = canvas->CreateObject("camera");
        auto* cameraTransform = canvas->AttachComponent<JBro::Component::Transform2D>(cameraObject);        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(cameraObject);
        cameraTransform->position = {2.0f, 3.0f};
        camera->primary = true;
        camera->orthographicSize = 10.0f;
        auto* object = canvas->CreateObject("sprites");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);        transform->position = {5.0f, 7.0f};
        transform->rotation = 1.57079632679f;
        for (int index = 0; index < 70; ++index)
        {
            auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
            Check(sprite != nullptr, "integration sprites must attach");
            sprite->renderOrder = 69 - index;
            sprite->size = {2.0f, 4.0f};
            sprite->pivot = {0.0f, 0.0f};
            sprite->flip = JBro::Component::SpriteFlip::Horizontal;
        }
        {
#if defined(_MSC_VER) && defined(_DEBUG)
            FrameAllocationProbe allocationProbe;
#endif
            JBro::Diagnostics::ComponentLookupCounters::Reset();
            framework.Update(0.0f);
            Check(framework.GetRenderWorld()->GetSpriteCount() == 70, "default systems must collect all sprites");
            Check(framework.GetRenderWorld()->GetSprite(0).renderOrder == 0, "collection must be sorted before submission");
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "host must open the renderer frame");
            Check(framework.Render() == JBro::RenderResult::Submitted, "framework must submit across the 64-item batch boundary");
            Check(module.device.commands.drawIndexedInstancedCount == 0, "framework must only collect before EndFrame");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "host must finish the renderer frame");
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        Check(frameAllocations == 0, "framework update, extraction, sorting and renderer submission must not allocate on the CRT heap");
#endif
        // 매 프레임 경로의 타입 조회 예산이다(§3.4). dereferences 가 lookups 보다
        // 훨씬 크면 오브젝트마다 후보를 줄줄이 따라가고 있다는 뜻이다.
        const std::size_t frameLookups = JBro::Diagnostics::ComponentLookupCounters::Get().lookups;
        const std::size_t frameDereferences = JBro::Diagnostics::ComponentLookupCounters::Get().dereferences;
        std::cout << "  [measure] 70-sprite frame: lookups=" << frameLookups
            << " dereferences=" << frameDereferences << std::endl;
        Check(frameLookups <= 3 * 71,
            "a flat 70-sprite frame must not need more than a few type lookups per object");
        Check(frameDereferences <= 2 * frameLookups,
            "a type lookup must not walk far past the component it wants");
        Check(module.device.uploadedInstanceCount == 70 && module.device.commands.drawIndexedInstancedCount == 1,
            "70 converted packets must become a single GPU instance upload and draw");
        const auto close = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
        const auto& world = module.device.firstInstanceWorld;
        Check(close(world.linear[0], 0.0f) && close(world.linear[1], -4.0f)
            && close(world.linear[2], -2.0f) && close(world.linear[3], 0.0f)
            && close(world.translation[0], 3.0f) && close(world.translation[1], 6.0f),
            "uploaded affine must apply flip, size, pivot and rotation in column-vector convention");
        Check(close(world.depth, 0.0f), "a sprite without a depth buffer must stay on the z=0 plane");
        Check(close(module.device.firstInstanceTint[3], 1.0f),
            "the tint must follow the transform at its own attribute offset, not overlap it");
        // 이 크기가 셰이더 입력 레이아웃과 같은 계약이다. 4x4 시절은 인스턴스당 80B 였고, UV 사각형(D-113)이 16B 를
        // 더해 60B 다 - 여전히 4x4 보다 작다.
        Check(module.device.lastInstanceUploadBytes == 70 * 60,
            "70 sprites must upload 60 bytes each, not the 80 the 4x4 packet cost");
        const auto& vp = module.device.commands.viewProjection.values;
        Check(close(vp[0], 0.05f) && close(vp[5], 0.1f) && close(vp[3], -0.1f) && close(vp[7], -0.3f),
            "camera projection must use half-height, aspect ratio and inverse translation");
        Check(module.device.waitIdleCount == 0, "framework frame must not wait for GPU idle");

        // 무효한 dt 는 프레임을 새로 열지 않는다. 지난 프레임 내용이 그대로 남아야 한다.
        framework.Update(std::numeric_limits<float>::quiet_NaN());
        Check(framework.GetRenderWorld()->GetSpriteCount() == 70,
            "a non-finite delta time must not blank the collected frame");
        framework.Update(-1.0f);
        Check(framework.GetRenderWorld()->GetSpriteCount() == 70,
            "a negative delta time must not blank the collected frame");

        Check(renderer.ResizeSurface({100, 200}), "integration surface must resize");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready
            && framework.Render() == JBro::RenderResult::Submitted, "framework must render at resized extent");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "resized frame must finish");
        Check(close(module.device.commands.viewProjection.values[0], 0.2f)
            && close(module.device.commands.viewport.height, 200.0f), "resize must refresh projection and viewport");

        Check(canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object) != nullptr, "overflow sprite must attach");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "overflow frame must begin");
        Check(framework.Render() == JBro::RenderResult::Failed, "collection overflow must reach the host");
        renderer.AbortFrame();
        Check(module.device.abortFrameCount == 1 && module.device.endFrameCount == 2,
            "failed frame must abort without presenting partial content");
        Check(module.device.waitIdleCount == 0, "abort must not wait for GPU idle");
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr && framework.GetRenderWorld()->GetSpriteCount() == 0,
            "shutdown must discard both canvas and stale frame packets");
        Check(framework.Initialize(context), "framework must support project reopening");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready
            && framework.Render() == JBro::RenderResult::NothingToSubmit,
            "empty reopened project must report nothing to submit, not success");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "reopened frame must finish");
        Check(renderer.GetLastFrameStats().spriteCount == 0, "reopened project must not replay stale sprites");
    }

    void TestHandlesRemainCompactValues()
    {
        static_assert(sizeof(JBro::BufferHandle) == 8);
        static_assert(sizeof(JBro::TextureHandle) == 8);
        static_assert(sizeof(JBro::SwapchainHandle) == 8);
        static_assert(sizeof(JBro::GraphicsPipelineHandle) == 8);
        static_assert(std::is_trivially_copyable_v<JBro::BufferHandle>);
        static_assert(std::is_trivially_copyable_v<JBro::TextureHandle>);
        static_assert(std::is_trivially_copyable_v<JBro::SwapchainHandle>);
        static_assert(std::is_trivially_copyable_v<JBro::GraphicsPipelineHandle>);
    }

    void TestRendererCollectsBeforeRecording()
    {
        FakeModule module;
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.surface = {99};
        config.surfaceExtent = {1280, 720};
        config.maxViews = 2;
        config.maxSpriteSubmissions = 2;
        config.maxMeshSubmissions = 1;
        config.validation = true;

        Check(renderer.Initialize(module, config), "renderer must initialize from an RHI module");
        Check(module.createDeviceCount == 1, "renderer must create exactly one device");
        Check(module.device.createSwapchainCount == 1, "renderer must create exactly one swapchain");
        Check(module.device.swapchainDesc.surface.value == 99, "swapchain must use the configured surface");
        Check(module.device.swapchainDesc.extent.width == 1280, "swapchain width must be forwarded");
        Check(module.lastCreateInfo.enableValidation, "validation setting must reach the RHI");

        JBro::CameraParams camera;
        Check(false == renderer.BeginView(camera), "a view must not begin outside a frame");
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "frame must begin");
        Check(renderer.BeginView(camera), "view must begin inside a frame");

        JBro::SpriteSubmit sprites[2];
        Check(renderer.SubmitSprites({sprites, 2}), "reserved sprite packet range must be accepted");
        Check(module.device.commands.beginRenderPassCount == 0,
            "sprite submission must collect packets without recording RHI commands");
        Check(module.device.commands.drawIndexedInstancedCount == 0,
            "sprite submission must not draw before frame compilation");
        Check(false == renderer.SubmitSprite(sprites[0]), "submission beyond fixed capacity must be rejected");

        Check(renderer.EndView(), "active view must end");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "frame must end");
        Check(module.device.commands.beginRenderPassCount == 1,
            "renderer must record one render pass after packet collection");
        Check(module.device.commands.endRenderPassCount == 1,
            "renderer must close every recorded render pass");
        Check(module.device.commands.setPipelineCount == 1,
            "one sprite batch must bind one graphics pipeline");
        Check(module.device.commands.setVertexBufferCount == 2,
            "one sprite batch must bind geometry and instance streams");
        Check(module.device.commands.setIndexBufferCount == 1,
            "one sprite batch must bind one index stream");
        Check(module.device.commands.setGraphicsConstantsCount == 1,
            "one sprite view must upload one view-projection constant block");
        Check(module.device.commands.drawIndexedInstancedCount == 1,
            "two collected sprites must collapse into one instanced draw");
        Check(module.device.writeBufferCount == 3,
            "renderer must use two initialization writes and one frame-bulk instance write");

        const JBro::RendererFrameStats stats = renderer.GetLastFrameStats();
        Check(stats.viewCount == 1, "completed frame must report one view");
        Check(stats.spriteCount == 2, "completed frame must report collected sprites");
        Check(stats.droppedSpriteCount == 1, "capacity overflow must be visible in diagnostics");
        Check(module.device.endFrameCount == 1, "renderer must submit one completed frame");
        Check(module.device.waitIdleCount == 0, "normal frame completion must never wait for the GPU to idle");

        Check(renderer.ResizeSurface({1920, 1080}), "idle renderer must resize its swapchain");
        Check(module.device.resizedExtent.width == 1920, "new swapchain width must be forwarded");

        renderer.Shutdown();
        Check(module.device.waitIdleCount == 1, "shutdown must wait for outstanding GPU work");
        Check(module.device.destroySwapchainCount == 1, "shutdown must destroy the swapchain");
        Check(module.destroyDeviceCount == 1, "shutdown must destroy the device");
        Check(module.device.destroyPipelineCount == 3,
            "shutdown must destroy the built-in sprite, sprite-over-depth and mesh pipelines");
    }
    struct HostOverlayProbe
    {
        int calls = 0;
        JBro::TextureHandle backBuffer;
    };

    bool RecordHostOverlay(
        JBro::IRHICommandContext&,
        JBro::TextureHandle backBuffer,
        std::uint32_t,
        void* user)
    {
        auto* probe = static_cast<HostOverlayProbe*>(user);
        ++probe->calls;
        probe->backBuffer = backBuffer;
        return true;
    }

    // **에디터 프레임의 배선이 호스트까지 닿는가.** 게임 화면은 텍스처로 가고,
    // 백버퍼에 낼 것이 없어도 프레임은 살아 있어야 에디터 UI 가 거기에 얹힌다(D-63).
    // **메시 등록은 입력을 검사하고 프레임 밖에서만 된다**(D-106). 정점 밖을 가리키는 색인은 GPU 가
    // 쓰레기를 읽는 길이라 여기서 막는다.
    void TestMeshRegistrationValidatesItsInput()
    {
        FakeModule module;
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.surface.value = 1;
        config.maxMeshSubmissions = 4;
        Check(renderer.Initialize(module, config), "the renderer must initialize");
        const JBro::MeshVertex vertices[3] = {};
        const std::uint32_t indices[3] = {0, 1, 2};
        const std::uint32_t outOfRange[3] = {0, 1, 3};
        const std::uint32_t notTriangles[4] = {0, 1, 2, 0};
        const JBro::AssetHandle mesh = renderer.RegisterMesh({vertices, 3}, {indices, 3});
        Check(mesh.generation != 0, "a sane mesh must register");
        Check(renderer.GetMeshCount() == 1, "and be counted");
        Check(renderer.RegisterMesh({vertices, 3}, {outOfRange, 3}).generation == 0,
            "an index past the vertices is refused");
        Check(renderer.RegisterMesh({vertices, 3}, {notTriangles, 4}).generation == 0,
            "an index count that is not triangles is refused");
        Check(renderer.RegisterMesh({vertices, 0}, {indices, 3}).generation == 0, "and so is an empty mesh");
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "a frame must begin");
        Check(renderer.RegisterMesh({vertices, 3}, {indices, 3}).generation == 0,
            "nothing registers inside a frame");
        renderer.AbortFrame();
        renderer.UnregisterMesh(mesh);
        Check(renderer.GetMeshCount() == 0, "unregistering takes it off the count");
        const JBro::AssetHandle again = renderer.RegisterMesh({vertices, 3}, {indices, 3});
        Check(again.index == mesh.index && again.generation == mesh.generation + 1,
            "the slot is reused under a new generation so the old handle is dead");
        renderer.UnregisterMesh(mesh);
        Check(renderer.GetMeshCount() == 1, "and the old handle no longer unregisters anything");
        renderer.Shutdown();
    }

    // **같은 메시는 드로우 하나다**(D-110). 제출 순서가 섞여도 업로드가 메시별로 모으고, 같은 메시 안에서는
    // 제출 순서가 지켜진다. 등록되지 않은 핸들은 세어 버리고 드로우에도 인스턴스에도 끼지 않는다.
    void TestMeshesSharingAHandleDrawAsOneInstancedCall()
    {
        FakeModule module;
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.surface.value = 1;
        config.maxMeshSubmissions = 8;
        Check(renderer.Initialize(module, config), "the renderer must initialize");
        const JBro::MeshVertex vertices[3] = {};
        const std::uint32_t indices[3] = {0, 1, 2};
        const JBro::AssetHandle first = renderer.RegisterMesh({vertices, 3}, {indices, 3});
        const JBro::AssetHandle second = renderer.RegisterMesh({vertices, 3}, {indices, 3});
        Check(first.generation != 0 && second.generation != 0 && first.index != second.index,
            "two meshes must register into different slots");
        JBro::AssetHandle stale = first;
        stale.generation += 1;

        FakeCommandContext& commands = module.device.commands;
        const std::uint32_t drawsBefore = commands.drawIndexedInstancedCount;
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "a frame must begin");
        JBro::CameraParams camera;
        Check(renderer.BeginView(camera), "a view must open");
        // 제출 순서: 1, 2, 1, 낡은 것, 1, 2. 종류는 둘, 낡은 것 하나.
        const JBro::AssetHandle order[6] = {first, second, first, stale, first, second};
        for (const JBro::AssetHandle& handle : order)
        {
            JBro::MeshSubmit submit;
            submit.mesh = handle;
            Check(renderer.SubmitMesh(submit), "every submit is accepted - the stale one is only dropped at upload");
        }
        Check(renderer.EndView(), "the view must close");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the frame must end");
        const JBro::RendererFrameStats stats = renderer.GetLastFrameStats();
        Check(stats.meshCount == 6 && stats.droppedMeshCount == 1, "six were submitted and the stale one was dropped");
        Check(commands.drawIndexedInstancedCount == drawsBefore + 2,
            "two mesh kinds must be two draws, not five");
        Check(commands.draws[drawsBefore].instanceCount == 3 && commands.draws[drawsBefore].firstInstance == 0,
            "the first mesh's three instances come first as one draw");
        Check(commands.draws[drawsBefore + 1].instanceCount == 2 && commands.draws[drawsBefore + 1].firstInstance == 3,
            "and the second mesh's two follow it");
        renderer.Shutdown();
    }

    void TestTheHostHandsTheEditorItsFrame()
    {
        FakeModule module;
        HostPlatform platform;
        platform.module = &module;
        HostFramework framework;
        framework.platform = &platform;
        JBro::EngineInstance engine;
        framework.engine = &engine;
        JBro::EngineConfig config;
        config.window.visible = false;
        Check(InitializeHost(engine, config, platform, module, framework),
            "the host must initialize");

        JBro::Renderer* renderer = engine.GetRenderer();
        Check(renderer != nullptr, "the host must expose its renderer");

        // ① 타깃을 주지 않으면 백버퍼로 간다. 게임 실행이 그것이다.
        framework.submitView = true;
        Check(engine.Tick(0.016f), "the plain frame must tick");
        Check(module.device.commands.lastColorAttachment == JBro::TextureHandle{2, 1},
            "with no target the view must go to the back buffer");

        // ② 타깃을 주면 그 텍스처로 간다. 같은 렌더 경로이고 목적지만 다르다.
        const JBro::TextureHandle gameView = module.device.CreateTexture({});
        JBro::FrameTarget target;
        target.texture = gameView;
        target.extent = {320, 240};
        Check(engine.SetGameViewTarget(target), "the host must take a game view target");
        Check(engine.Tick(0.016f), "the aimed frame must tick");
        Check(module.device.commands.lastColorAttachment == gameView,
            "the view must go to the texture the editor asked for");

        // ③ 게임이 낼 것이 없는 프레임. 오버레이가 없으면 버린다.
        framework.submitView = false;
        framework.renderResult = JBro::RenderResult::NothingToSubmit;
        std::uint32_t aborts = module.device.abortFrameCount;
        std::uint32_t presents = module.device.endFrameCount;
        Check(engine.Tick(0.016f), "an empty frame must not stop the loop");
        Check(module.device.abortFrameCount == aborts + 1
                && module.device.endFrameCount == presents,
            "with nothing to show and nobody to draw, the frame is thrown away");

        // ④ **오버레이를 걸면 같은 프레임이 살아남는다.** 에디터에서는 게임 화면이
        // 텍스처로 가서 백버퍼가 비는 것이 정상이고, 그 프레임을 버리면 UI 도 사라진다.
        HostOverlayProbe probe;
        Check(renderer->SetFrameOverlay(&RecordHostOverlay, &probe),
            "the overlay must attach outside a frame");
        aborts = module.device.abortFrameCount;
        presents = module.device.endFrameCount;
        Check(engine.Tick(0.016f), "the frame with an overlay must tick");
        Check(probe.calls == 1, "the overlay must be asked to draw");
        Check(probe.backBuffer == JBro::TextureHandle{2, 1},
            "and be handed the back buffer, not the game view texture");
        Check(module.device.endFrameCount == presents + 1
                && module.device.abortFrameCount == aborts,
            "and the frame must be presented rather than thrown away");

        // ⑤ **뷰 기록을 끈 타깃이다.** 게임 뷰 패널이 보이지 않는 프레임이 이것이다(D-63).
        // 제출은 받되 렌더 패스를 열지 않고, 텍스처는 그대로 둔다.
        framework.submitView = true;
        framework.renderResult = JBro::RenderResult::Submitted;
        target.recordViews = false;
        Check(engine.SetGameViewTarget(target), "the host must take a target that skips views");
        const std::uint32_t passes = module.device.commands.beginRenderPassCount;
        presents = module.device.endFrameCount;
        Check(engine.Tick(0.016f), "the frame that skips its views must tick");
        Check(module.device.commands.beginRenderPassCount == passes,
            "a target that does not want views must open no render pass for them");
        Check(renderer->GetLastFrameStats().skippedViewCount == 1
                && renderer->GetLastFrameStats().viewCount == 1,
            "and the submitted view must be counted as skipped, not dropped");
        Check(module.device.endFrameCount == presents + 1,
            "while the frame itself is still presented for the overlay");
        target.recordViews = true;
        Check(engine.SetGameViewTarget(target), "the host must take the target back");
        Check(engine.Tick(0.016f), "the next frame must tick");
        Check(module.device.commands.beginRenderPassCount > passes
                && module.device.commands.lastColorAttachment == gameView,
            "and record its views to the texture again");

        Check(renderer->SetFrameOverlay(nullptr, nullptr), "the overlay must detach");
        engine.Shutdown();
    }

}

int RunRendererContractTests()
{
    TestHandlesRemainCompactValues();
    TestRendererCollectsBeforeRecording();
    TestFrameworkSubmitsTransformedBatches();
    TestEngineHostLifecycle();
    TestTheHostHandsTheEditorItsFrame();
    TestMeshRegistrationValidatesItsInput();
    TestMeshesSharingAHandleDrawAsOneInstancedCall();
    TestProjectSwitchPreservesProcessResources();
    std::cout << "Renderer contract tests passed.\n";
    return 0;
}
