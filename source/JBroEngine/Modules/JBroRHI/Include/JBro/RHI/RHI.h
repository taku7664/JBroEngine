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

    struct SamplerHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const SamplerHandle&) const = default;
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
        Float4,
        // 바이트 넷을 0..1 로 읽는다. UI 와 스프라이트의 정점 색이 이 모양이다 —
        // float4 로 부풀리면 정점마다 12바이트를 더 옮기고 변환도 한 번 더 한다.
        UByte4Norm
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

    enum class FilterMode : std::uint8_t
    {
        // 텍셀 하나를 그대로 집는다. 픽셀 아트와 폰트 아틀라스처럼 흐려지면 안 되는 것에 쓴다.
        Nearest,
        Linear
    };

    enum class AddressMode : std::uint8_t
    {
        // 가장자리 텍셀을 늘린다. 아틀라스에서 옆 칸이 새어 들어오지 않는다.
        ClampToEdge,
        Repeat
    };

    struct SamplerDesc
    {
        FilterMode  minFilter = FilterMode::Linear;
        FilterMode  magFilter = FilterMode::Linear;
        AddressMode addressU  = AddressMode::ClampToEdge;
        AddressMode addressV  = AddressMode::ClampToEdge;
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
        // 이 파이프라인이 픽셀 셰이더에서 읽는 텍스처와 샘플러의 수다.
        // `t0..t(N-1)`, `s0..s(N-1)` 에 순서대로 묶인다.
        //
        // **개수를 파이프라인이 미리 말해야 한다.** 백엔드가 이것으로 루트 시그니처를 만들고,
        // 그것은 파이프라인과 함께 만들어져 바뀌지 않는다. 그리기 직전에 알 수 있는 값이 아니다.
        std::uint32_t sampledTextureCount = 0;
        std::uint32_t samplerCount = 0;
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
        // 슬롯에 텍스처와 샘플러를 묶는다. 슬롯 번호는 셰이더의 `t`/`s` 레지스터 번호다.
        // 파이프라인이 선언한 개수를 넘는 슬롯은 거절한다 — 루트 시그니처에 자리가 없다.
        //
        // 그리기 직전까지 기억만 하고, 실제 묶는 것은 드로우 호출에서 한 번에 한다.
        // 한 드로우에 필요한 것이 다 모인 뒤라야 디스크립터를 연속으로 놓을 수 있다.
        virtual bool SetTexture(std::uint32_t slot, TextureHandle texture) = 0;
        virtual bool SetSampler(std::uint32_t slot, SamplerHandle sampler) = 0;
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
        // 텍스처 한 면에 픽셀을 올린다. 바이트는 행 패딩 없이 빽빽한 것으로 받는다 —
        // GPU 쪽 행 정렬은 백엔드의 사정이다(`ReadTexture` 와 같은 계약이다).
        //
        // **GPU 가 끝날 때까지 기다린다.** 로드 경로이고 프레임 안에서는 거절한다.
        virtual bool WriteTexture(
            TextureHandle texture,
            std::uint32_t mipLevel,
            JArrayView<std::byte> data) = 0;
        virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
        virtual void DestroySampler(SamplerHandle sampler) = 0;
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

        // 그래픽 API 의 검증 레이어가 남긴 오류·손상 메시지의 수다.
        //
        // **이것은 테스트가 붙잡는 손잡이다.** D3D12 는 잘못된 리소스 상태나 잘못된
        // 시저 같은 것을 대개 조용히 지나간다 - 그 기계의 드라이버가 알아서 주워 담고,
        // 그림이 맞게 나오니 테스트도 통과한다. 그러고는 다른 기계에서 깨진다.
        // 픽셀만 보아서는 그 차이를 볼 수 없고, 검증 레이어만이 말해 준다.
        //
        // 검증을 켜지 않았거나 백엔드가 그런 것을 갖고 있지 않으면 0 이다.
        virtual std::uint32_t GetValidationErrorCount() const
        {
            return 0;
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
