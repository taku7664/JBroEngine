#include <JBro/Graphics/Renderer.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
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

        void SetViewport(const JBro::Viewport&) override
        {
        }

        void SetScissor(const JBro::ScissorRect&) override
        {
        }

        std::uint32_t beginRenderPassCount = 0;
        std::uint32_t endRenderPassCount = 0;
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

        JBro::TextureHandle CreateTexture(const JBro::TextureDesc&) override
        {
            return {1, 1};
        }

        void DestroyTexture(JBro::TextureHandle) override
        {
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

    void TestHandlesRemainCompactValues()
    {
        static_assert(sizeof(JBro::BufferHandle) == 8);
        static_assert(sizeof(JBro::TextureHandle) == 8);
        static_assert(sizeof(JBro::SwapchainHandle) == 8);
        static_assert(std::is_trivially_copyable_v<JBro::BufferHandle>);
        static_assert(std::is_trivially_copyable_v<JBro::TextureHandle>);
        static_assert(std::is_trivially_copyable_v<JBro::SwapchainHandle>);
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
        Check(false == renderer.SubmitSprite(sprites[0]), "submission beyond fixed capacity must be rejected");

        Check(renderer.EndView(), "active view must end");
        Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "frame must end");
        Check(module.device.commands.beginRenderPassCount == 1,
            "renderer must record one render pass after packet collection");
        Check(module.device.commands.endRenderPassCount == 1,
            "renderer must close every recorded render pass");

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
    }
}

int RunRendererContractTests()
{
    TestHandlesRemainCompactValues();
    TestRendererCollectsBeforeRecording();
    std::cout << "Renderer contract tests passed.\n";
    return 0;
}
