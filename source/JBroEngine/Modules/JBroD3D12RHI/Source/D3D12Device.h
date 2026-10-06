#pragma once

#include <JBro/RHI/RHI.h>

#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Internal
{
    using Microsoft::WRL::ComPtr;

    inline constexpr UInt32 InvalidRootParameter = ~UInt32{0};
    // 한 드로우가 묶을 수 있는 텍스처·샘플러 수다. 넘으면 조용히 자르지 않고 거절한다.
    inline constexpr UInt32 MaxBoundTextures = 8;
    inline constexpr UInt32 MaxBoundSamplers = 4;

    // 한 패스에 붙일 수 있는 색 첨부의 수다. 배리어 배열이 이 크기로 잡히므로
    // 컨텍스트와 패스 시작 코드가 같은 값을 봐야 한다.
    constexpr UInt32 MaxColorAttachments = 8;

    struct D3D12RenderTargetBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES* state = nullptr;
        // Sampled 로도 만든 텍스처다. 패스가 끝나면 셰이더가 읽을 수 있는 상태로 되돌린다.
        Bool sampled = false;
    };

    // 깊이 첨부 하나. 렌더 타깃과 달리 샘플링으로 되돌릴 일이 없다 - 깊이 텍스처는 그리기 전용이다.
    struct D3D12DepthStencilBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        D3D12_RESOURCE_STATES* state = nullptr;
    };

    struct D3D12BufferBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        UInt64 size = 0;
        BufferUsage usage = BufferUsage::None;
    };

    struct D3D12PipelineBinding
    {
        ID3D12PipelineState* pipeline = nullptr;
        ID3D12RootSignature* rootSignature = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        UInt32 pushConstantCount = 0;
        UInt32 sampledTextureCount = 0;
        UInt32 samplerCount = 0;
        // 루트 파라미터에서의 자리다. 없으면 InvalidRootParameter 다 —
        // 0 은 유효한 자리이므로 0 으로 "없음" 을 나타낼 수 없다.
        UInt32 textureTableParameter = InvalidRootParameter;
        UInt32 samplerTableParameter = InvalidRootParameter;
    };

    class D3D12Device;

    class D3D12CommandContext final : public IRHICommandContext
    {
    public:
        void Initialize(
            D3D12Device& device,
            ID3D12GraphicsCommandList& commandList,
            ID3D12GraphicsCommandList4* commandList4,
            Bool nativeRenderPasses);
        void Reset();
        Bool IsRenderPassActive() const;

        Bool BeginRenderPass(const RenderPassDesc& desc) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& viewport) override;
        void SetScissor(const ScissorRect& scissor) override;
        Bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        Bool SetVertexBuffer(
            UInt32 slot,
            BufferHandle buffer,
            UInt32 stride,
            std::size_t offset) override;
        Bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) override;
        Bool SetGraphicsConstants(JArrayView<std::byte> data) override;
        Bool SetTexture(UInt32 slot, TextureHandle texture) override;
        Bool SetSampler(UInt32 slot, SamplerHandle sampler) override;
        Bool DrawIndexedInstanced(
            UInt32 indexCount,
            UInt32 instanceCount,
            UInt32 firstIndex,
            Int32 baseVertex,
            UInt32 firstInstance) override;
        Bool CopyTexture(TextureHandle source, TextureHandle destination) override;

    private:
        D3D12Device* m_device = nullptr;
        ID3D12GraphicsCommandList* m_commandList = nullptr;
        ID3D12GraphicsCommandList4* m_commandList4 = nullptr;
        // 드로우 직전까지 들고 있다가 한 번에 묶는다. 디스크립터 테이블은 연속된 자리를
        // 가리키므로, 슬롯이 다 모이기 전에는 어디에 놓을지 정할 수 없다.
        Bool BindPendingDescriptors();

        ID3D12Resource* m_discardAtEnd[MaxColorAttachments] = {};
        UInt32 m_discardAtEndCount = 0;
        // 패스가 끝날 때 셰이더 읽기 상태로 되돌릴 렌더 타깃이다.
        // **패스 안에서는 되돌릴 수 없다** - 네이티브 렌더 패스 안의 배리어는 불법이다.
        D3D12RenderTargetBinding m_sampledAtEnd[MaxColorAttachments] = {};
        UInt32 m_sampledAtEndCount = 0;
        UInt32 m_activePushConstantCount = 0;
        // 이 프레임에 스테이징한 테이블이다. 같은 디스크립터 묶음이 다시 오면 복사하지 않고 그 테이블을 다시
        // 건다(D-110). ImGui 는 드로우마다 같은 폰트 아틀라스를 묶으므로 이것이 없으면 드로우마다 복사 하나와
        // 프레임 몫 하나를 쓴다 - 드로우 512 개에서 샘플러 몫이 바닥나 나머지가 조용히 빠졌다.
        static constexpr UInt32 CachedTables = 16;
        struct StagedTable
        {
            D3D12_CPU_DESCRIPTOR_HANDLE keys[MaxBoundTextures] = {};
            UInt32 count = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE table = {};
        };
        StagedTable m_textureTables[CachedTables] = {};
        StagedTable m_samplerTables[CachedTables] = {};
        UInt32 m_textureTableCount = 0;
        UInt32 m_samplerTableCount = 0;
        UInt32 m_textureTableCursor = 0;
        UInt32 m_samplerTableCursor = 0;
        // 지금 루트에 걸린 테이블. 같으면 다시 걸지 않는다. 파이프라인이 바뀌면 비운다.
        D3D12_GPU_DESCRIPTOR_HANDLE m_boundTextureTable = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_boundSamplerTable = {};
        Bool FindStagedTable(const StagedTable* tables, UInt32 tableCount,
            const D3D12_CPU_DESCRIPTOR_HANDLE* keys, UInt32 count, D3D12_GPU_DESCRIPTOR_HANDLE& table) const;
        void RememberStagedTable(StagedTable* tables, UInt32& tableCount, UInt32& cursor,
            const D3D12_CPU_DESCRIPTOR_HANDLE* keys, UInt32 count, D3D12_GPU_DESCRIPTOR_HANDLE table);
        D3D12PipelineBinding m_activePipeline;
        D3D12_CPU_DESCRIPTOR_HANDLE m_pendingTextures[MaxBoundTextures] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE m_pendingSamplers[MaxBoundSamplers] = {};
        Bool m_nativeRenderPasses = false;
        Bool m_renderPassActive = false;
        Bool m_pipelineActive = false;
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
        UInt32 generation = 1;
        UInt32 backBufferGeneration = 1;
        UInt32 descriptorStride = 0;
        Bool occupied = false;
    };

    struct D3D12BufferState
    {
        ComPtr<ID3D12Resource> resource;
        BufferDesc desc;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        void* mappedData = nullptr;
        UInt64 allocatedSize = 0;
        UInt64 retirementFence = 0;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct D3D12PipelineState
    {
        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> pipeline;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        UInt64 retirementFence = 0;
        UInt32 generation = 1;
        UInt32 pushConstantCount = 0;
        UInt32 sampledTextureCount = 0;
        UInt32 samplerCount = 0;
        UInt32 textureTableParameter = InvalidRootParameter;
        UInt32 samplerTableParameter = InvalidRootParameter;
        Bool occupied = false;
    };

    struct D3D12SamplerState
    {
        SamplerDesc desc;
        // 셰이더에서 보이지 않는 자리에 만들어 둔다. 드로우 때 보이는 힙으로 복사한다.
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct D3D12TextureState
    {
        ComPtr<ID3D12Resource> resource;
        TextureDesc desc;
        // 프레임이 시작할 때의 상태다. 프레임을 버리면(`AbortFrame`) 기록만 되고 실행되지 않은 배리어가
        // 추적 상태를 어긋나게 하므로 여기로 되돌린다.
        D3D12_RESOURCE_STATES stateAtFrameStart = D3D12_RESOURCE_STATE_COMMON;
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor = {};
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor = {};
        // Sampled 로 만든 텍스처만 갖는다. 없으면 ptr 이 0 이다.
        D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceDescriptor = {};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        UInt64 retirementFence = 0;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    class D3D12Device final : public IRHIDevice
    {
    public:
        D3D12Device() = default;
        D3D12Device(const D3D12Device&) = delete;
        D3D12Device& operator=(const D3D12Device&) = delete;

        Bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        Bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        UInt32 GetValidationErrorCount() const override;
        UInt32 GetFramesInFlight() const override;
        Bool WriteBuffer(
            BufferHandle buffer,
            std::size_t offset,
            JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        Bool WriteTexture(
            TextureHandle texture,
            UInt32 mipLevel,
            JArrayView<std::byte> data) override;
        Bool WriteTextureRegion(TextureHandle texture, UInt32 mipLevel, UInt32 x, UInt32 y,
            UInt32 width, UInt32 height, JArrayView<std::byte> data, UInt32 rowPitch) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) override;

        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        Bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;

        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;

        Bool ResolveRenderTarget(TextureHandle texture, D3D12RenderTargetBinding& binding);
        // `DepthStencil` 로 만든 텍스처만 받는다. 백버퍼는 깊이가 될 수 없다.
        Bool ResolveDepthStencil(TextureHandle texture, D3D12DepthStencilBinding& binding);
        Bool ResolveBuffer(BufferHandle buffer, D3D12BufferBinding& binding);
        Bool ResolveGraphicsPipeline(
            GraphicsPipelineHandle pipeline,
            D3D12PipelineBinding& binding);
        // 셰이더에서 읽을 수 있는 텍스처인지 보고 그 디스크립터를 준다.
        Bool ResolveSampledTexture(TextureHandle texture, D3D12_CPU_DESCRIPTOR_HANDLE& descriptor);
        Bool ResolveSampler(SamplerHandle sampler, D3D12_CPU_DESCRIPTOR_HANDLE& descriptor);
        // 이번 프레임의 링에서 연속된 자리를 잡아 스테이징 디스크립터를 복사해 넣는다.
        Bool StageShaderResources(
            const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
            UInt32 count,
            D3D12_GPU_DESCRIPTOR_HANDLE& table);
        Bool StageSamplers(
            const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
            UInt32 count,
            D3D12_GPU_DESCRIPTOR_HANDLE& table);

    private:
        static constexpr UInt32 MaxFramesInFlight = 3;
        static constexpr UInt32 MaxSwapchains = 8;
        static constexpr UInt32 MaxBackBuffers = 4;
        static constexpr UInt32 BackBufferTextureBase = 1;
        static constexpr UInt32 MaxBuffers = 1024;
        static constexpr UInt32 MaxTextures = 512;
        static constexpr UInt32 MaxGraphicsPipelines = 256;
        static constexpr UInt32 MaxSamplers = 64;
        // 프레임 슬롯마다 제 몫을 갖는다. 프레임이 겹쳐 도는 동안 앞 프레임이 쓰던
        // 디스크립터를 덮어쓰지 않게 하려면 링을 프레임별로 갈라야 한다.
        // 프레임 하나가 스테이징할 수 있는 텍스처 디스크립터 수다. 드로우가 같은 텍스처를 다시 묶으면
        // 컨텍스트가 이미 스테이징한 테이블을 재사용하므로(D-110) 이 수는 **서로 다른 묶음**의 수다.
        static constexpr UInt32 ShaderVisibleTexturesPerFrame = 4096;
        // **D3D12 는 셰이더 가시 샘플러 힙을 2048개로 제한한다.** 프레임 수를 곱한 값이
        // 그 안에 들어와야 하므로 프레임당 512 가 사실상의 상한이다.
        // (텍스처 쪽 힙은 백만 단위라 그런 제약이 없다.)
        static constexpr UInt32 ShaderVisibleSamplersPerFrame = 512;
        static_assert(ShaderVisibleSamplersPerFrame * MaxFramesInFlight <= 2048,
            "a shader visible sampler heap cannot hold more than 2048 descriptors");
        static constexpr UInt32 TextureResourceBase =
            BackBufferTextureBase + MaxSwapchains * MaxBackBuffers;
        static constexpr UInt64 PendingRetirementFence = ~UInt64{0};

        D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain);
        const D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain) const;
        Bool BuildBackBuffers(UInt32 swapchainIndex, D3D12SwapchainState& state);
        void ReleaseBackBuffers(D3D12SwapchainState& state);
        Bool WaitForFence(UInt64 fenceValue);
        // 핸들이 백버퍼든 일반 텍스처든 자원과 현재 상태를 찾아 준다. 읽기 경로 전용이다.
        Bool ResolveReadableTexture(
            TextureHandle texture,
            ID3D12Resource*& resource,
            D3D12_RESOURCE_STATES*& state,
            TextureDesc& desc);
        void CollectRetiredResources();
        void AssignPendingRetirementFences(UInt64 fenceValue);
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
        UInt64 m_frameFenceValues[MaxFramesInFlight] = {};
        UInt64 m_nextFenceValue = 1;
        UInt64 m_lastSubmittedFenceValue = 0;
        UInt64 m_frameSerial = 0;
        UInt64 m_activeFrameSerial = 0;
        UInt32 m_activeSwapchainIndex = 0;
        UInt32 m_activeFrameSlot = 0;
        HANDLE m_fenceEvent = nullptr;
        UInt32 m_textureRenderTargetDescriptorStride = 0;
        UInt32 m_textureDepthStencilDescriptorStride = 0;
        UInt32 m_shaderResourceDescriptorStride = 0;
        UInt32 m_samplerDescriptorStride = 0;
        // 이번 프레임 슬롯 안에서 몇 개까지 썼는지. BeginFrame 에서 0 으로 되돌린다.
        UInt32 m_shaderVisibleTextureCursor = 0;
        UInt32 m_shaderVisibleSamplerCursor = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        Bool m_tearingSupported = false;
        Bool m_frameActive = false;
        Bool m_hasPendingRetirementFence = false;
    };
}
