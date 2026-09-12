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

    struct GraphicsPipelineHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const GraphicsPipelineHandle&) const = default;
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

    enum class ShaderStage : std::uint8_t
    {
        Vertex = 1u << 0u,
        Pixel = 1u << 1u
    };

    constexpr ShaderStage operator|(ShaderStage left, ShaderStage right) noexcept
    {
        return static_cast<ShaderStage>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    enum class VertexFormat : std::uint8_t
    {
        Float2,
        Float3,
        Float4
    };

    enum class VertexStepMode : std::uint8_t
    {
        Vertex,
        Instance
    };

    enum class IndexFormat : std::uint8_t
    {
        UInt16,
        UInt32
    };

    enum class PrimitiveTopology : std::uint8_t
    {
        TriangleList
    };

    enum class BlendMode : std::uint8_t
    {
        Opaque,
        Alpha
    };

    enum class CullMode : std::uint8_t
    {
        None,
        Front,
        Back
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

    struct ShaderBytecode
    {
        const void* data = nullptr;
        std::uint32_t size = 0;
    };

    struct VertexAttributeDesc
    {
        std::uint32_t shaderLocation = 0;
        std::uint32_t offset = 0;
        VertexFormat format = VertexFormat::Float2;
    };

    struct VertexBufferLayoutDesc
    {
        std::uint32_t stride = 0;
        VertexStepMode stepMode = VertexStepMode::Vertex;
        JArrayView<VertexAttributeDesc> attributes;
    };

    struct GraphicsPipelineDesc
    {
        ShaderBytecode vertexShader;
        ShaderBytecode pixelShader;
        JArrayView<VertexBufferLayoutDesc> vertexBuffers;
        JArrayView<TextureFormat> colorFormats;
        TextureFormat depthFormat = TextureFormat::Unknown;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        BlendMode blend = BlendMode::Opaque;
        CullMode cull = CullMode::Back;
        ShaderStage pushConstantStages = ShaderStage::Vertex;
        std::uint32_t pushConstantBytes = 0;
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
        virtual bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;
        virtual bool SetVertexBuffer(
            std::uint32_t slot,
            BufferHandle buffer,
            std::uint32_t stride,
            std::size_t offset) = 0;
        virtual bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) = 0;
        virtual bool SetGraphicsConstants(JArrayView<std::byte> data) = 0;
        virtual bool DrawIndexedInstanced(
            std::uint32_t indexCount,
            std::uint32_t instanceCount,
            std::uint32_t firstIndex,
            std::int32_t baseVertex,
            std::uint32_t firstInstance) = 0;
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

    // 읽어 온 이미지의 모양이다. 목적지에는 행 패딩 없이 빽빽하게 쓴다 —
    // GPU 쪽 행 정렬은 백엔드의 사정이고 부르는 쪽이 알 필요가 없다.
    struct TextureReadback
    {
        Extent2D      extent;
        TextureFormat format = TextureFormat::Unknown;
        std::uint32_t rowPitch = 0;
        std::uint32_t writtenBytes = 0;
    };

    class IRHIDevice
    {
    public:
        virtual ~IRHIDevice() = default;

        virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        virtual bool WriteBuffer(
            BufferHandle buffer,
            std::size_t offset,
            JArrayView<std::byte> data) = 0;
        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        virtual void DestroyTexture(TextureHandle texture) = 0;
        virtual GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        virtual void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;

        virtual SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) = 0;
        virtual void DestroySwapchain(SwapchainHandle swapchain) = 0;
        virtual bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) = 0;

        virtual BeginFrameResult BeginFrame(SwapchainHandle swapchain) = 0;
        virtual FrameStatus EndFrame(const FrameContext& frame) = 0;
        virtual void AbortFrame(const FrameContext& frame) = 0;
        virtual FrameStatus GetStatus() const = 0;
        virtual void WaitIdle() = 0;

        // 텍스처 내용을 CPU 로 읽는다. **진단과 테스트 경로다** — GPU 가 끝날 때까지
        // 기다리므로 매 프레임 경로에서 부르지 않는다(§9). 프레임이 열려 있으면 실패한다.
        //
        // 기본 구현은 false 다. 읽기 경로가 없는 백엔드도 있을 수 있고, 없는 것과
        // 실패한 것을 호출부가 구분할 필요는 없다 — 둘 다 "읽지 못했다"이다.
        virtual bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result)
        {
            (void)texture;
            (void)destination;
            (void)destinationSize;
            (void)result;
            return false;
        }
    };

    class IRHIModule : public IModule
    {
    public:
        virtual GraphicsApi GetApi() const = 0;
        virtual IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) = 0;
        virtual void DestroyDevice(IRHIDevice* device) = 0;
    };
}
