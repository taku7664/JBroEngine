#include <JBro/Graphics/Renderer.h>

#include <JBro/Framework2D/Framework2D.h>

#include <cmath>
#include <cstring>
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
        bool BeginRenderPass(const JBro::RenderPassDesc&) override
        {
            ++beginRenderPassCount;
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

        bool DrawIndexedInstanced(
            std::uint32_t,
            std::uint32_t,
            std::uint32_t,
            std::int32_t,
            std::uint32_t) override
        {
            ++drawIndexedInstancedCount;
            return true;
        }

        std::uint32_t beginRenderPassCount = 0;
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
            if (data.size >= 80 && data.size % 80 == 0)
            {
                uploadedInstanceCount = data.size / 80;
                std::memcpy(firstInstanceWorld.values, data.data, sizeof(firstInstanceWorld.values));
            }
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
            return true;
        }

        JBro::BeginFrameResult BeginFrame(JBro::SwapchainHandle) override
        {
            ++beginFrameCount;

            JBro::BeginFrameResult result;
            result.status = JBro::FrameStatus::Ready;
            result.frame.serial = beginFrameCount;
            result.frame.slot = 0;
            result.frame.backBuffer = {2, 1};
            result.frame.commands = &commands;
            return result;
        }

        JBro::FrameStatus EndFrame(const JBro::FrameContext&) override
        {
            ++endFrameCount;
            return JBro::FrameStatus::Ready;
        }

        void AbortFrame(const JBro::FrameContext&) override
        {
            ++abortFrameCount;
        }

        JBro::FrameStatus GetStatus() const override
        {
            return JBro::FrameStatus::Ready;
        }

        void WaitIdle() override
        {
            ++waitIdleCount;
        }

        FakeCommandContext commands;
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
        JBro::Matrix4x4 firstInstanceWorld;
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
        Check(canvas->GetSystems().GetSystemCount() == 4, "canvas must own the four implemented default systems");
        auto* cameraObject = canvas->CreateObject("camera");
        auto* cameraTransform = canvas->AttachComponent<JBro::Component::Transform2D>(cameraObject);
        canvas->AttachComponent<JBro::Component::WorldTransform2D>(cameraObject);
        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(cameraObject);
        cameraTransform->position = {2.0f, 3.0f};
        camera->primary = true;
        camera->orthographicSize = 10.0f;
        auto* object = canvas->CreateObject("sprites");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        canvas->AttachComponent<JBro::Component::WorldTransform2D>(object);
        transform->position = {5.0f, 7.0f};
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
            framework.Update(0.0f);
            Check(framework.GetRenderWorld()->GetSpriteCount() == 70, "default systems must collect all sprites");
            Check(framework.GetRenderWorld()->GetSprites()[0].renderOrder == 0, "collection must be sorted before submission");
            Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "host must open the renderer frame");
            Check(framework.Render(), "framework must submit across the 64-item batch boundary");
            Check(module.device.commands.drawIndexedInstancedCount == 0, "framework must only collect before EndFrame");
            Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "host must finish the renderer frame");
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        Check(frameAllocations == 0, "framework update, extraction, sorting and renderer submission must not allocate on the CRT heap");
#endif
        Check(module.device.uploadedInstanceCount == 70 && module.device.commands.drawIndexedInstancedCount == 1,
            "70 converted packets must become a single GPU instance upload and draw");
        const auto close = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
        const auto& world = module.device.firstInstanceWorld.values;
        Check(close(world[0], 0.0f) && close(world[1], -4.0f) && close(world[3], 3.0f)
            && close(world[4], -2.0f) && close(world[5], 0.0f) && close(world[7], 6.0f),
            "uploaded matrix must apply flip, size, pivot and rotation in column-vector convention");
        const auto& vp = module.device.commands.viewProjection.values;
        Check(close(vp[0], 0.05f) && close(vp[5], 0.1f) && close(vp[3], -0.1f) && close(vp[7], -0.3f),
            "camera projection must use half-height, aspect ratio and inverse translation");
        Check(module.device.waitIdleCount == 0, "framework frame must not wait for GPU idle");

        Check(renderer.ResizeSurface({100, 200}), "integration surface must resize");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready && framework.Render(), "framework must render at resized extent");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "resized frame must finish");
        Check(close(module.device.commands.viewProjection.values[0], 0.2f)
            && close(module.device.commands.viewport.height, 200.0f), "resize must refresh projection and viewport");

        Check(canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object) != nullptr, "overflow sprite must attach");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "overflow frame must begin");
        Check(false == framework.Render(), "collection overflow must reach the host");
        renderer.AbortFrame();
        Check(module.device.abortFrameCount == 1 && module.device.endFrameCount == 2,
            "failed frame must abort without presenting partial content");
        Check(module.device.waitIdleCount == 0, "abort must not wait for GPU idle");
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr && framework.GetRenderWorld()->GetSpriteCount() == 0,
            "shutdown must discard both canvas and stale frame packets");
        Check(framework.Initialize(context), "framework must support project reopening");
        framework.Update(0.0f);
        Check(renderer.BeginFrame() == JBro::FrameStatus::Ready && framework.Render(), "empty reopened project must submit no views");
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
        Check(module.device.destroyPipelineCount == 1,
            "shutdown must destroy the built-in sprite pipeline");
    }
}

int RunRendererContractTests()
{
    TestHandlesRemainCompactValues();
    TestRendererCollectsBeforeRecording();
    TestFrameworkSubmitsTransformedBatches();
    std::cout << "Renderer contract tests passed.\n";
    return 0;
}
