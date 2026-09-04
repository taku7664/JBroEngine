#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Platform.h>

namespace JBro
{
    enum class GraphicsApi : std::uint8_t
    {
        D3D12,
        Vulkan,
        WebGPU
    };

    struct BufferHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const BufferHandle&) const = default;
    };

    struct TextureHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const TextureHandle&) const = default;
    };

    struct SwapchainHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const SwapchainHandle&) const = default;
    };

    enum class BufferUsage : std::uint32_t
    {
        None = 0,
        Vertex = 1u << 0u,
        Index = 1u << 1u,
        Constant = 1u << 2u,
        CopySource = 1u << 3u,
        CopyDestination = 1u << 4u
    };

    constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept
    {
        return static_cast<BufferUsage>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    enum class TextureUsage : std::uint32_t
    {
        None = 0,
        Sampled = 1u << 0u,
        RenderTarget = 1u << 1u,
        DepthStencil = 1u << 2u,
        Storage = 1u << 3u,
        CopySource = 1u << 4u,
        CopyDestination = 1u << 5u
    };

    constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) noexcept
    {
        return static_cast<TextureUsage>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    enum class MemoryType : std::uint8_t
    {
        Device,
        Upload,
        Readback
    };

    enum class TextureFormat : std::uint8_t
    {
        Unknown,
        RGBA8Unorm,
        RGBA8UnormSrgb,
        BGRA8Unorm,
        BGRA8UnormSrgb,
        RGBA16Float,
        D32Float
    };

    enum class PresentMode : std::uint8_t
    {
        VSync,
        Immediate
    };

    enum class LoadOperation : std::uint8_t
    {
        Load,
        Clear,
        Discard
    };

    enum class StoreOperation : std::uint8_t
    {
        Store,
        Discard
    };

    enum class FrameStatus : std::uint8_t
    {
        Ready,
        Skipped,
        SurfaceLost,
        DeviceLost,
        InvalidState
    };

    struct Extent2D
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct ClearColor
    {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
    };

    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct ScissorRect
    {
        std::int32_t left = 0;
        std::int32_t top = 0;
        std::int32_t right = 0;
        std::int32_t bottom = 0;
    };

    struct BufferDesc
    {
        std::size_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryType memory = MemoryType::Device;
    };

    struct TextureDesc
    {
        Extent2D extent;
        std::uint32_t depthOrLayers = 1;
        std::uint32_t mipLevels = 1;
        std::uint32_t sampleCount = 1;
        TextureFormat format = TextureFormat::Unknown;
        TextureUsage usage = TextureUsage::None;
    };

    struct SwapchainDesc
    {
        SurfaceHandle surface;
        Extent2D extent;
        TextureFormat format = TextureFormat::BGRA8Unorm;
        PresentMode presentMode = PresentMode::VSync;
        std::uint8_t bufferCount = 3;
        std::uint8_t maxFramesInFlight = 2;
    };

    struct ColorAttachmentDesc
    {
        TextureHandle texture;
        LoadOperation loadOperation = LoadOperation::Load;
        StoreOperation storeOperation = StoreOperation::Store;
        ClearColor clearColor;
    };

    struct DepthStencilAttachmentDesc
    {
        TextureHandle texture;
        LoadOperation depthLoadOperation = LoadOperation::Load;
        StoreOperation depthStoreOperation = StoreOperation::Store;
        LoadOperation stencilLoadOperation = LoadOperation::Load;
        StoreOperation stencilStoreOperation = StoreOperation::Store;
        float clearDepth = 1.0f;
        std::uint8_t clearStencil = 0;
    };

    struct RenderPassDesc
    {
        JArrayView<ColorAttachmentDesc> colorAttachments;
        const DepthStencilAttachmentDesc* depthStencilAttachment = nullptr;
    };

    class IRHICommandContext
    {
    public:
        virtual ~IRHICommandContext() = default;

        virtual bool BeginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void EndRenderPass() = 0;
        virtual void SetViewport(const Viewport& viewport) = 0;
        virtual void SetScissor(const ScissorRect& scissor) = 0;
    };

    struct FrameContext
    {
        std::uint64_t serial = 0;
        std::uint32_t slot = 0;
        TextureHandle backBuffer;
        IRHICommandContext* commands = nullptr;
    };

    struct BeginFrameResult
    {
        FrameStatus status = FrameStatus::InvalidState;
        FrameContext frame;
    };

    struct RHIDeviceCreateInfo
    {
        bool enableValidation = false;
    };

    class IRHIDevice
    {
    public:
        virtual ~IRHIDevice() = default;

        virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        virtual void DestroyTexture(TextureHandle texture) = 0;

        virtual SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) = 0;
        virtual void DestroySwapchain(SwapchainHandle swapchain) = 0;
        virtual bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) = 0;

        virtual BeginFrameResult BeginFrame(SwapchainHandle swapchain) = 0;
        virtual FrameStatus EndFrame(const FrameContext& frame) = 0;
        virtual void AbortFrame(const FrameContext& frame) = 0;
        virtual FrameStatus GetStatus() const = 0;
        virtual void WaitIdle() = 0;
    };

    class IRHIModule : public IModule
    {
    public:
        virtual GraphicsApi GetApi() const = 0;
        virtual IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) = 0;
        virtual void DestroyDevice(IRHIDevice* device) = 0;
    };
}
