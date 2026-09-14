#pragma once

#include <JBro/RHI/RHI.h>

#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace JBro::Internal
{
    using Microsoft::WRL::ComPtr;

    inline constexpr std::uint32_t InvalidRootParameter = ~std::uint32_t{0};
    // 한 드로우가 묶을 수 있는 텍스처·샘플러 수다. 넘으면 조용히 자르지 않고 거절한다.
    inline constexpr std::uint32_t MaxBoundTextures = 8;
    inline constexpr std::uint32_t MaxBoundSamplers = 4;

    // 한 패스에 붙일 수 있는 색 첨부의 수다. 배리어 배열이 이 크기로 잡히므로
    // 컨텍스트와 패스 시작 코드가 같은 값을 봐야 한다.
    constexpr std::uint32_t MaxColorAttachments = 8;

    struct D3D12RenderTargetBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES* state = nullptr;
        // Sampled 로도 만든 텍스처다. 패스가 끝나면 셰이더가 읽을 수 있는 상태로 되돌린다.
        bool sampled = false;
    };

    struct D3D12BufferBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        std::uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
    };

    struct D3D12PipelineBinding
    {
        ID3D12PipelineState* pipeline = nullptr;
        ID3D12RootSignature* rootSignature = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::uint32_t pushConstantCount = 0;
        std::uint32_t sampledTextureCount = 0;
        std::uint32_t samplerCount = 0;
        // 루트 파라미터에서의 자리다. 없으면 InvalidRootParameter 다 —
        // 0 은 유효한 자리이므로 0 으로 "없음" 을 나타낼 수 없다.
        std::uint32_t textureTableParameter = InvalidRootParameter;
        std::uint32_t samplerTableParameter = InvalidRootParameter;
    };

    class D3D12Device;

    class D3D12CommandContext final : public IRHICommandContext
    {
    public:
        void Initialize(
            D3D12Device& device,
            ID3D12GraphicsCommandList& commandList,
            ID3D12GraphicsCommandList4* commandList4,
            bool nativeRenderPasses);
        void Reset();
        bool IsRenderPassActive() const;

        bool BeginRenderPass(const RenderPassDesc& desc) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& viewport) override;
        void SetScissor(const ScissorRect& scissor) override;
        bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        bool SetVertexBuffer(
            std::uint32_t slot,
            BufferHandle buffer,
            std::uint32_t stride,
            std::size_t offset) override;
        bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) override;
        bool SetGraphicsConstants(JArrayView<std::byte> data) override;
        bool SetTexture(std::uint32_t slot, TextureHandle texture) override;
        bool SetSampler(std::uint32_t slot, SamplerHandle sampler) override;
        bool DrawIndexedInstanced(
            std::uint32_t indexCount,
            std::uint32_t instanceCount,
            std::uint32_t firstIndex,
            std::int32_t baseVertex,
            std::uint32_t firstInstance) override;

    private:
        D3D12Device* m_device = nullptr;
        ID3D12GraphicsCommandList* m_commandList = nullptr;
        ID3D12GraphicsCommandList4* m_commandList4 = nullptr;
        // 드로우 직전까지 들고 있다가 한 번에 묶는다. 디스크립터 테이블은 연속된 자리를
        // 가리키므로, 슬롯이 다 모이기 전에는 어디에 놓을지 정할 수 없다.
        bool BindPendingDescriptors();

        ID3D12Resource* m_discardAtEnd[MaxColorAttachments] = {};
        std::uint32_t m_discardAtEndCount = 0;
        // 패스가 끝날 때 셰이더 읽기 상태로 되돌릴 렌더 타깃이다.
        // **패스 안에서는 되돌릴 수 없다** - 네이티브 렌더 패스 안의 배리어는 불법이다.
        D3D12RenderTargetBinding m_sampledAtEnd[MaxColorAttachments] = {};
        std::uint32_t m_sampledAtEndCount = 0;
        std::uint32_t m_activePushConstantCount = 0;
        D3D12PipelineBinding m_activePipeline;
        D3D12_CPU_DESCRIPTOR_HANDLE m_pendingTextures[MaxBoundTextures] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE m_pendingSamplers[MaxBoundSamplers] = {};
        bool m_nativeRenderPasses = false;
        bool m_renderPassActive = false;
        bool m_pipelineActive = false;
    };

    struct D3D12BackBuffer
    {
        ComPtr<ID3D12Resource> resource;
        TextureHandle handle;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PRESENT;
    };

    struct D3D12SwapchainState
    {
        SwapchainDesc desc;
        ComPtr<IDXGISwapChain3> swapchain;
        ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
        D3D12BackBuffer backBuffers[4];
        std::uint32_t generation = 1;
        std::uint32_t backBufferGeneration = 1;
        std::uint32_t descriptorStride = 0;
        bool occupied = false;
    };

    struct D3D12BufferState
    {
        ComPtr<ID3D12Resource> resource;
        BufferDesc desc;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        void* mappedData = nullptr;
        std::uint64_t allocatedSize = 0;
        std::uint64_t retirementFence = 0;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D12PipelineState
    {
        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> pipeline;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::uint64_t retirementFence = 0;
        std::uint32_t generation = 1;
        std::uint32_t pushConstantCount = 0;
        std::uint32_t sampledTextureCount = 0;
        std::uint32_t samplerCount = 0;
        std::uint32_t textureTableParameter = InvalidRootParameter;
        std::uint32_t samplerTableParameter = InvalidRootParameter;
        bool occupied = false;
    };

    struct D3D12SamplerState
    {
        SamplerDesc desc;
        // 셰이더에서 보이지 않는 자리에 만들어 둔다. 드로우 때 보이는 힙으로 복사한다.
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D12TextureState
    {
        ComPtr<ID3D12Resource> resource;
        TextureDesc desc;
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor = {};
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor = {};
        // Sampled 로 만든 텍스처만 갖는다. 없으면 ptr 이 0 이다.
        D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceDescriptor = {};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        std::uint64_t retirementFence = 0;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    class D3D12Device final : public IRHIDevice
    {
    public:
        D3D12Device() = default;
        D3D12Device(const D3D12Device&) = delete;
        D3D12Device& operator=(const D3D12Device&) = delete;

        bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        std::uint32_t GetValidationErrorCount() const override;
        bool WriteBuffer(
            BufferHandle buffer,
            std::size_t offset,
            JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        bool WriteTexture(
            TextureHandle texture,
            std::uint32_t mipLevel,
            JArrayView<std::byte> data) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) override;

        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;

        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;

        bool ResolveRenderTarget(TextureHandle texture, D3D12RenderTargetBinding& binding);
        bool ResolveBuffer(BufferHandle buffer, D3D12BufferBinding& binding);
        bool ResolveGraphicsPipeline(
            GraphicsPipelineHandle pipeline,
            D3D12PipelineBinding& binding);
        // 셰이더에서 읽을 수 있는 텍스처인지 보고 그 디스크립터를 준다.
        bool ResolveSampledTexture(TextureHandle texture, D3D12_CPU_DESCRIPTOR_HANDLE& descriptor);
        bool ResolveSampler(SamplerHandle sampler, D3D12_CPU_DESCRIPTOR_HANDLE& descriptor);
        // 이번 프레임의 링에서 연속된 자리를 잡아 스테이징 디스크립터를 복사해 넣는다.
        bool StageShaderResources(
            const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
            std::uint32_t count,
            D3D12_GPU_DESCRIPTOR_HANDLE& table);
        bool StageSamplers(
            const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
            std::uint32_t count,
            D3D12_GPU_DESCRIPTOR_HANDLE& table);

    private:
        static constexpr std::uint32_t MaxFramesInFlight = 3;
        static constexpr std::uint32_t MaxSwapchains = 8;
        static constexpr std::uint32_t MaxBackBuffers = 4;
        static constexpr std::uint32_t BackBufferTextureBase = 1;
        static constexpr std::uint32_t MaxBuffers = 1024;
        static constexpr std::uint32_t MaxTextures = 512;
        static constexpr std::uint32_t MaxGraphicsPipelines = 256;
        static constexpr std::uint32_t MaxSamplers = 64;
        // 프레임 슬롯마다 제 몫을 갖는다. 프레임이 겹쳐 도는 동안 앞 프레임이 쓰던
        // 디스크립터를 덮어쓰지 않게 하려면 링을 프레임별로 갈라야 한다.
        static constexpr std::uint32_t ShaderVisibleTexturesPerFrame = 1024;
        // **D3D12 는 셰이더 가시 샘플러 힙을 2048개로 제한한다.** 프레임 수를 곱한 값이
        // 그 안에 들어와야 하므로 프레임당 512 가 사실상의 상한이다.
        // (텍스처 쪽 힙은 백만 단위라 그런 제약이 없다.)
        static constexpr std::uint32_t ShaderVisibleSamplersPerFrame = 512;
        static_assert(ShaderVisibleSamplersPerFrame * MaxFramesInFlight <= 2048,
            "a shader visible sampler heap cannot hold more than 2048 descriptors");
        static constexpr std::uint32_t TextureResourceBase =
            BackBufferTextureBase + MaxSwapchains * MaxBackBuffers;
        static constexpr std::uint64_t PendingRetirementFence = ~std::uint64_t{0};

        D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain);
        const D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain) const;
        bool BuildBackBuffers(std::uint32_t swapchainIndex, D3D12SwapchainState& state);
        void ReleaseBackBuffers(D3D12SwapchainState& state);
        bool WaitForFence(std::uint64_t fenceValue);
        // 핸들이 백버퍼든 일반 텍스처든 자원과 현재 상태를 찾아 준다. 읽기 경로 전용이다.
        bool ResolveReadableTexture(
            TextureHandle texture,
            ID3D12Resource*& resource,
            D3D12_RESOURCE_STATES*& state,
            TextureDesc& desc);
        void CollectRetiredResources();
        void AssignPendingRetirementFences(std::uint64_t fenceValue);
        void ReleaseAllResources();
        void MarkDeviceLost();

        ComPtr<IDXGIFactory6> m_factory;
        ComPtr<IDXGIAdapter1> m_adapter;
        ComPtr<ID3D12Device> m_device;
        // 검증을 켰을 때만 있다. 오류와 손상만 남기도록 걸러 둔다.
        ComPtr<ID3D12InfoQueue> m_infoQueue;
        ComPtr<ID3D12CommandQueue> m_graphicsQueue;
        ComPtr<ID3D12Fence> m_fence;
        ComPtr<ID3D12DescriptorHeap> m_textureRenderTargetHeap;
        ComPtr<ID3D12DescriptorHeap> m_textureDepthStencilHeap;
        // 스테이징이다. 셰이더에서 보이지 않고, 자원 수명 동안 그 자리에 있다.
        ComPtr<ID3D12DescriptorHeap> m_textureShaderResourceHeap;
        ComPtr<ID3D12DescriptorHeap> m_samplerStagingHeap;
        // 셰이더에서 보이는 링이다. 드로우 때 스테이징에서 여기로 복사한다.
        ComPtr<ID3D12DescriptorHeap> m_shaderVisibleTextureHeap;
        ComPtr<ID3D12DescriptorHeap> m_shaderVisibleSamplerHeap;
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[MaxFramesInFlight];
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12GraphicsCommandList4> m_commandList4;
        D3D12CommandContext m_commandContext;
        D3D12SwapchainState m_swapchains[MaxSwapchains];
        D3D12BufferState m_buffers[MaxBuffers];
        D3D12TextureState m_textures[MaxTextures];
        D3D12PipelineState m_graphicsPipelines[MaxGraphicsPipelines];
        D3D12SamplerState m_samplers[MaxSamplers];
        std::uint64_t m_frameFenceValues[MaxFramesInFlight] = {};
        std::uint64_t m_nextFenceValue = 1;
        std::uint64_t m_lastSubmittedFenceValue = 0;
        std::uint64_t m_frameSerial = 0;
        std::uint64_t m_activeFrameSerial = 0;
        std::uint32_t m_activeSwapchainIndex = 0;
        std::uint32_t m_activeFrameSlot = 0;
        HANDLE m_fenceEvent = nullptr;
        std::uint32_t m_textureRenderTargetDescriptorStride = 0;
        std::uint32_t m_textureDepthStencilDescriptorStride = 0;
        std::uint32_t m_shaderResourceDescriptorStride = 0;
        std::uint32_t m_samplerDescriptorStride = 0;
        // 이번 프레임 슬롯 안에서 몇 개까지 썼는지. BeginFrame 에서 0 으로 되돌린다.
        std::uint32_t m_shaderVisibleTextureCursor = 0;
        std::uint32_t m_shaderVisibleSamplerCursor = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        bool m_tearingSupported = false;
        bool m_frameActive = false;
        bool m_hasPendingRetirementFence = false;
    };
}
