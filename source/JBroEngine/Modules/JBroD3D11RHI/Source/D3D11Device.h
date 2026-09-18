#pragma once

#include <JBro/RHI/RHI.h>

#include <JBro/Types/Array.h>

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstdint>

namespace JBro::Internal
{
    using Microsoft::WRL::ComPtr;

    constexpr std::uint32_t MaxColorAttachments = 8;
    constexpr std::uint32_t MaxBoundTextures = 8;
    constexpr std::uint32_t MaxBoundSamplers = 4;
    constexpr std::uint32_t MaxVertexSlots = 8;

    class D3D11Device;

    // 즉시 컨텍스트를 `IRHICommandContext` 로 감싼다. D3D11 은 명령을 미루지 않으므로 슬롯을 모아
    // 묶는 단계가 없다 - 부르는 즉시 컨텍스트에 건다.
    class D3D11CommandContext final : public IRHICommandContext
    {
    public:
        void Bind(D3D11Device* device, ID3D11DeviceContext* context);
        void Reset();

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

        bool IsRenderPassActive() const
        {
            return m_renderPassActive;
        }

    private:
        D3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        GraphicsPipelineHandle m_activePipeline;
        std::uint32_t m_activePushConstantBytes = 0;
        ShaderStage m_activePushConstantStages = ShaderStage::Vertex;
        // 참조를 든다. 파이프라인이 패스 중간에 지워져도 상수 버퍼는 살아 있다.
        ComPtr<ID3D11Buffer> m_activeConstantBuffer;
        bool m_renderPassActive = false;
        bool m_pipelineActive = false;
    };

    struct D3D11SwapchainState
    {
        SwapchainDesc desc;
        ComPtr<IDXGISwapChain1> swapchain;
        ComPtr<ID3D11Texture2D> backBuffer;
        ComPtr<ID3D11RenderTargetView> backBufferView;
        // 제시 직전의 백버퍼 사본이다. 플립 모델은 제시한 버퍼를 다시 읽을 수 없으므로(버퍼 0 은 다음
        // 프레임의 것이 된다) 되읽기 계약(`ReadTexture`, 진단 전용)은 이 사본으로 지킨다.
        ComPtr<ID3D11Texture2D> presentedCopy;
        std::uint32_t generation = 1;
        std::uint32_t backBufferGeneration = 1;
        bool occupied = false;
    };

    struct D3D11BufferState
    {
        ComPtr<ID3D11Buffer> buffer;
        BufferDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D11TextureState
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTargetView;
        ComPtr<ID3D11DepthStencilView> depthStencilView;
        ComPtr<ID3D11ShaderResourceView> shaderResourceView;
        TextureDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D11SamplerState
    {
        ComPtr<ID3D11SamplerState> sampler;
        SamplerDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct D3D11PipelineState
    {
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11BlendState> blendState;
        ComPtr<ID3D11RasterizerState> rasterizerState;
        ComPtr<ID3D11DepthStencilState> depthStencilState;
        // 푸시 상수 대신이다. D3D11 에는 루트 상수가 없어 파이프라인마다 상수 버퍼 하나를 b0 에 건다.
        ComPtr<ID3D11Buffer> constantBuffer;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::uint32_t pushConstantBytes = 0;
        ShaderStage pushConstantStages = ShaderStage::Vertex;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    class D3D11Device final : public IRHIDevice
    {
    public:
        D3D11Device() = default;
        D3D11Device(const D3D11Device&) = delete;
        D3D11Device& operator=(const D3D11Device&) = delete;

        bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        bool WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        bool WriteTexture(TextureHandle texture, std::uint32_t mipLevel, JArrayView<std::byte> data) override;
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
        bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        std::uint32_t GetFramesInFlight() const override;
        std::uint32_t GetValidationErrorCount() const override;

        // 컨텍스트가 쓰는 해석 함수들.
        bool ResolveRenderTargetView(TextureHandle texture, ID3D11RenderTargetView*& view);
        bool ResolveDepthStencilView(TextureHandle texture, ID3D11DepthStencilView*& view);
        bool ResolveShaderResourceView(TextureHandle texture, ID3D11ShaderResourceView*& view);
        bool ResolveBuffer(BufferHandle buffer, ID3D11Buffer*& native, BufferDesc& desc);
        bool ResolveSampler(SamplerHandle sampler, ID3D11SamplerState*& native);
        D3D11PipelineState* ResolvePipeline(GraphicsPipelineHandle pipeline);
        ID3D11Device* GetNativeDevice() const
        {
            return m_device.Get();
        }

    private:
        static constexpr std::uint32_t MaxSwapchains = 8;
        static constexpr std::uint32_t BackBufferTextureBase = 1;
        static constexpr std::uint32_t MaxBuffers = 1024;
        static constexpr std::uint32_t MaxTextures = 512;
        static constexpr std::uint32_t MaxGraphicsPipelines = 256;
        static constexpr std::uint32_t MaxSamplers = 64;
        static constexpr std::uint32_t TextureResourceBase = BackBufferTextureBase + MaxSwapchains;

        D3D11SwapchainState* FindSwapchain(SwapchainHandle swapchain);
        bool BuildBackBuffer(D3D11SwapchainState& state);
        bool ResolveReadableTexture(TextureHandle texture, ID3D11Texture2D*& resource, TextureDesc& desc);
        void MarkDeviceLost();

        ComPtr<IDXGIFactory2> m_factory;
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<ID3D11InfoQueue> m_infoQueue;
        D3D11CommandContext m_commandContext;
        D3D11SwapchainState m_swapchains[MaxSwapchains];
        D3D11BufferState m_buffers[MaxBuffers];
        D3D11TextureState m_textures[MaxTextures];
        D3D11PipelineState m_graphicsPipelines[MaxGraphicsPipelines];
        D3D11SamplerState m_samplers[MaxSamplers];
        std::uint64_t m_frameSerial = 0;
        std::uint32_t m_activeSwapchainIndex = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        bool m_frameActive = false;
        bool m_tearingSupported = false;
    };

    // 두 소스 파일이 함께 쓰는 변환.
    DXGI_FORMAT ToNativeFormat(TextureFormat format);
    std::uint32_t PixelSize(TextureFormat format);
}
