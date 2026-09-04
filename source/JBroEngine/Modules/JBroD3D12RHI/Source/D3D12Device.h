#pragma once

#include <JBro/RHI/RHI.h>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace JBro::Internal
{
    using Microsoft::WRL::ComPtr;

    struct D3D12RenderTargetBinding
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES* state = nullptr;
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

    private:
        D3D12Device* m_device = nullptr;
        ID3D12GraphicsCommandList* m_commandList = nullptr;
        ID3D12GraphicsCommandList4* m_commandList4 = nullptr;
        ID3D12Resource* m_discardAtEnd[8] = {};
        std::uint32_t m_discardAtEndCount = 0;
        bool m_nativeRenderPasses = false;
        bool m_renderPassActive = false;
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
        std::uint64_t retirementFence = 0;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D12TextureState
    {
        ComPtr<ID3D12Resource> resource;
        TextureDesc desc;
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor = {};
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor = {};
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
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;

        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;

        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;

        bool ResolveRenderTarget(TextureHandle texture, D3D12RenderTargetBinding& binding);

    private:
        static constexpr std::uint32_t MaxFramesInFlight = 3;
        static constexpr std::uint32_t MaxSwapchains = 8;
        static constexpr std::uint32_t MaxBackBuffers = 4;
        static constexpr std::uint32_t BackBufferTextureBase = 1;
        static constexpr std::uint32_t MaxBuffers = 1024;
        static constexpr std::uint32_t MaxTextures = 512;
        static constexpr std::uint32_t TextureResourceBase =
            BackBufferTextureBase + MaxSwapchains * MaxBackBuffers;
        static constexpr std::uint64_t PendingRetirementFence = ~std::uint64_t{0};

        D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain);
        const D3D12SwapchainState* FindSwapchain(SwapchainHandle swapchain) const;
        bool BuildBackBuffers(std::uint32_t swapchainIndex, D3D12SwapchainState& state);
        void ReleaseBackBuffers(D3D12SwapchainState& state);
        bool WaitForFence(std::uint64_t fenceValue);
        void CollectRetiredResources();
        void AssignPendingRetirementFences(std::uint64_t fenceValue);
        void ReleaseAllResources();
        void MarkDeviceLost();

        ComPtr<IDXGIFactory6> m_factory;
        ComPtr<IDXGIAdapter1> m_adapter;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_graphicsQueue;
        ComPtr<ID3D12Fence> m_fence;
        ComPtr<ID3D12DescriptorHeap> m_textureRenderTargetHeap;
        ComPtr<ID3D12DescriptorHeap> m_textureDepthStencilHeap;
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[MaxFramesInFlight];
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12GraphicsCommandList4> m_commandList4;
        D3D12CommandContext m_commandContext;
        D3D12SwapchainState m_swapchains[MaxSwapchains];
        D3D12BufferState m_buffers[MaxBuffers];
        D3D12TextureState m_textures[MaxTextures];
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
        FrameStatus m_status = FrameStatus::InvalidState;
        bool m_tearingSupported = false;
        bool m_frameActive = false;
        bool m_hasPendingRetirementFence = false;
    };
}
