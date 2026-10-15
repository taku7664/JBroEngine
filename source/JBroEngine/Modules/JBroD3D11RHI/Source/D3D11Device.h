#pragma once

#include <JBro/RHI/RHI.h>

#include <JBro/Types/Array.h>

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Internal
{
    using Microsoft::WRL::ComPtr;

    constexpr UInt32 MaxColorAttachments = 8;
    constexpr UInt32 MaxBoundTextures = 8;
    constexpr UInt32 MaxBoundSamplers = 4;
    // 상수 버퍼 슬롯은 b1..b7 이다. b0 은 파이프라인의 푸시 상수 버퍼가 쓴다.
    constexpr UInt32 MaxBoundConstantBuffers = 7;
    constexpr UInt32 MaxVertexSlots = 8;

    class D3D11Device;

    // 즉시 컨텍스트를 `IRHICommandContext` 로 감싼다. D3D11 은 명령을 미루지 않으므로 슬롯을 모아
    // 묶는 단계가 없다 - 부르는 즉시 컨텍스트에 건다.
    class D3D11CommandContext final : public IRHICommandContext
    {
    public:
        void Bind(D3D11Device* device, ID3D11DeviceContext* context);
        void Reset();

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
        Bool SetTextureMip(UInt32 slot, TextureHandle texture, UInt32 mipLevel) override;
        Bool SetSampler(UInt32 slot, SamplerHandle sampler) override;
        Bool SetConstantBuffer(UInt32 slot, BufferHandle buffer, std::size_t offset, std::size_t size) override;
        Bool DrawIndexedInstanced(
            UInt32 indexCount,
            UInt32 instanceCount,
            UInt32 firstIndex,
            Int32 baseVertex,
            UInt32 firstInstance) override;
        Bool DrawInstanced(
            UInt32 vertexCount,
            UInt32 instanceCount,
            UInt32 firstVertex,
            UInt32 firstInstance) override;
        Bool CopyTexture(TextureHandle source, TextureHandle destination) override;
        Bool SetComputePipeline(ComputePipelineHandle pipeline) override;
        Bool SetStorageTexture(UInt32 slot, TextureHandle texture, UInt32 mipLevel) override;
        Bool SetStorageBuffer(UInt32 slot, BufferHandle buffer) override;
        Bool Dispatch(UInt32 groupCountX, UInt32 groupCountY, UInt32 groupCountZ) override;

        Bool IsRenderPassActive() const
        {
            return m_renderPassActive;
        }

    private:
        // 두 그리기의 공통 앞단이다. 패스와 파이프라인이 있고 선언한 상수 버퍼가 다 묶였는지 본다.
        Bool PrepareDraw() const;
        Bool CanBind() const;
        // 단계 묶음에 컴퓨트가 있으면 CS 에도 건다. 지금 걸린 파이프라인의 단계다.
        Bool HasComputeStage(ShaderStage stages) const;
        Bool BindTexture(UInt32 slot, TextureHandle texture, UInt32 mipLevel);
        // 지금 패스가 그리는 자리다. 그 mip 을 읽으려 하면 거절한다(`SetTextureMip` 의 계약).
        RenderPassTargets m_passTargets;

        D3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        // 11.1 의 부분 묶기(`*SetConstantBuffers1`)다. 없는 기계면 null 이고 오프셋 0 만 묶는다.
        ComPtr<ID3D11DeviceContext1> m_context1;
        GraphicsPipelineHandle m_activePipeline;
        UInt32 m_activePushConstantBytes = 0;
        ShaderStage m_activePushConstantStages = ShaderStage::Vertex;
        // 이 파이프라인의 자리표다. 묶을 때 슬롯을 거르고 단계마다 건다.
        BindingLayoutDesc m_activeBindings;
        // 참조를 든다. 파이프라인이 패스 중간에 지워져도 상수 버퍼는 살아 있다.
        ComPtr<ID3D11Buffer> m_activeConstantBuffer;
        // b# 자리마다 묶였는지다(0 은 쓰지 않는다). 파이프라인을 바꾸면 비운다 - 다른 백엔드와 같은 계약이다.
        Bool m_constantBufferBound[MaxBoundConstantBuffers + 1] = {};
        // 패스 밖에 컴퓨트 파이프라인이 걸려 있다. `m_activeBindings` 가 그 자리표다.
        Bool m_computeActive = false;
        // u0..u7 자리다(u0..u3 텍스처, u4..u7 버퍼). 디스패치 때 한 번에 걸고 돈 뒤에 뗀다 - 걸어 둔 채면 다음 패스가 그 텍스처를 읽거나 그릴 때
        // D3D11 이 경고를 내며 떼어 낸다.
        static constexpr UInt32 StorageSlots = 8;
        ID3D11UnorderedAccessView* m_pendingStorage[StorageSlots] = {};
        Bool m_renderPassActive = false;
        Bool m_pipelineActive = false;
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
        UInt32 generation = 1;
        UInt32 backBufferGeneration = 1;
        Bool occupied = false;
    };

    struct D3D11BufferState
    {
        ComPtr<ID3D11Buffer> buffer;
        BufferDesc desc;
        // `Storage` 로 만든 버퍼만 갖는다(구조화 버퍼 UAV).
        ComPtr<ID3D11UnorderedAccessView> unorderedAccessView;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct D3D11TextureState
    {
        ComPtr<ID3D11Texture2D> texture;
        // 3D 텍스처면 위 대신 이것이 있다(읽기 전용, SRV 하나).
        ComPtr<ID3D11Texture3D> volume;
        // mip 마다 하나다. 그리기 경로에서 뷰를 만들지 않으려고 미리 다 만든다.
        ComPtr<ID3D11RenderTargetView> renderTargetViews[MaxTextureMipLevels];
        ComPtr<ID3D11DepthStencilView> depthStencilView;
        // 모든 mip 을 보는 뷰다.
        ComPtr<ID3D11ShaderResourceView> shaderResourceView;
        // mip 하나씩만 보는 뷰다(`SetTextureMip`). mip 이 둘 이상일 때만 만든다 - 하나뿐이면 위의 뷰가 곧 그 mip 이다.
        ComPtr<ID3D11ShaderResourceView> mipShaderResourceViews[MaxTextureMipLevels];
        // `Storage` 로 만든 텍스처만 갖는다. mip 마다 하나다(`SetStorageTexture`).
        ComPtr<ID3D11UnorderedAccessView> unorderedAccessViews[MaxTextureMipLevels];
        TextureDesc desc;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct D3D11SamplerState
    {
        ComPtr<ID3D11SamplerState> sampler;
        SamplerDesc desc;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    // 컴퓨트 파이프라인이다. 셰이더와 자리표뿐이다 - 상태 객체가 없다.
    struct D3D11ComputePipelineState
    {
        ComPtr<ID3D11ComputeShader> shader;
        BindingLayoutDesc bindings;
        UInt32 generation = 1;
        Bool occupied = false;
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
        UInt32 pushConstantBytes = 0;
        ShaderStage pushConstantStages = ShaderStage::Vertex;
        BindingLayoutDesc bindings;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    class D3D11Device final : public IRHIDevice
    {
    public:
        D3D11Device() = default;
        D3D11Device(const D3D11Device&) = delete;
        D3D11Device& operator=(const D3D11Device&) = delete;

        Bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        DeviceCaps GetCaps() const override;
        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        Bool WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        Bool WriteTexture(TextureHandle texture, UInt32 mipLevel, JArrayView<std::byte> data) override;
        Bool WriteTextureRegion(TextureHandle texture, UInt32 mipLevel, UInt32 x, UInt32 y,
            UInt32 width, UInt32 height, JArrayView<std::byte> data, UInt32 rowPitch) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        ComputePipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
        void DestroyComputePipeline(ComputePipelineHandle pipeline) override;
        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        Bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;
        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;
        Bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        UInt32 GetFramesInFlight() const override;
        UInt32 GetValidationErrorCount() const override;

        // 컨텍스트가 쓰는 해석 함수들.
        // 텍스처는 그 mip 하나의 뷰와 그 mip 의 크기로 풀린다. 백버퍼는 mip 0 만 있다.
        Bool ResolveRenderTargetView(TextureHandle texture, UInt32 mipLevel, ID3D11RenderTargetView*& view, Extent2D& extent);
        Bool ResolveDepthStencilView(TextureHandle texture, ID3D11DepthStencilView*& view);
        // `mipLevel` 이 `AllMips` 면 모든 mip 을 보는 뷰다.
        static constexpr UInt32 AllMips = ~UInt32{0};
        Bool ResolveShaderResourceView(TextureHandle texture, UInt32 mipLevel, ID3D11ShaderResourceView*& view);
        Bool ResolveBuffer(BufferHandle buffer, ID3D11Buffer*& native, BufferDesc& desc);
        Bool ResolveSampler(SamplerHandle sampler, ID3D11SamplerState*& native);
        D3D11PipelineState* ResolvePipeline(GraphicsPipelineHandle pipeline);
        D3D11ComputePipelineState* ResolveComputePipeline(ComputePipelineHandle pipeline);
        // 읽고 쓰는 텍스처의 mip 하나, 구조화 버퍼 하나의 UAV 다. `Storage` 로 만들지 않은 것은 거절한다.
        Bool ResolveStorageTexture(TextureHandle texture, UInt32 mipLevel, ID3D11UnorderedAccessView*& view);
        Bool ResolveStorageBuffer(BufferHandle buffer, ID3D11UnorderedAccessView*& view);
        ID3D11Device* GetNativeDevice() const
        {
            return m_device.Get();
        }

    private:
        static constexpr UInt32 MaxSwapchains = 8;
        static constexpr UInt32 BackBufferTextureBase = 1;
        static constexpr UInt32 MaxBuffers = 1024;
        static constexpr UInt32 MaxTextures = 512;
        static constexpr UInt32 MaxGraphicsPipelines = 256;
        static constexpr UInt32 MaxComputePipelines = 64;
        static constexpr UInt32 MaxSamplers = 64;
        static constexpr UInt32 TextureResourceBase = BackBufferTextureBase + MaxSwapchains;

        D3D11SwapchainState* FindSwapchain(SwapchainHandle swapchain);
        Bool BuildBackBuffer(D3D11SwapchainState& state);
        Bool ResolveReadableTexture(TextureHandle texture, ID3D11Texture2D*& resource, TextureDesc& desc);
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
        D3D11ComputePipelineState m_computePipelines[MaxComputePipelines];
        D3D11SamplerState m_samplers[MaxSamplers];
        UInt64 m_frameSerial = 0;
        UInt32 m_activeSwapchainIndex = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        Bool m_frameActive = false;
        Bool m_tearingSupported = false;
        // 오프셋이 있는 상수 버퍼 구간을 묶을 수 있다. 11.1 의 부분 묶기와 동적 상수 버퍼의 NO_OVERWRITE 가 둘 다 있어야
        // 참이다 - 링은 한 버퍼의 여러 구간을 한 프레임 안에 차례로 쓰고 묶는다.
        Bool m_constantBufferOffsets = false;
    };

    // 두 소스 파일이 함께 쓰는 변환.
    DXGI_FORMAT ToNativeFormat(TextureFormat format);
    UInt32 PixelSize(TextureFormat format);
}
