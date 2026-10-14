#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Size.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    enum class GraphicsApi : std::uint8_t
    {
        D3D12,
        Vulkan,
        WebGPU,
        // 두 번째 Windows 백엔드다(D-107). 값을 끝에 두어 저장된 숫자가 밀리지 않게 한다.
        D3D11
    };

    struct BufferHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;

        constexpr Bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const BufferHandle&) const = default;
    };

    struct TextureHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;

        constexpr Bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const TextureHandle&) const = default;
    };

    struct SwapchainHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;

        constexpr Bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const SwapchainHandle&) const = default;
    };

    struct GraphicsPipelineHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;

        constexpr Bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const GraphicsPipelineHandle&) const = default;
    };

    struct SamplerHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;

        constexpr Bool IsValid() const noexcept
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
        D32Float,
        // 후처리의 중간 텍스처 포맷이다(postprocess-plan §2.1). 값을 끝에 두어 앞의 숫자가 밀리지 않게 한다.
        // `R11G11B10Float` 는 알파가 없고 음수를 담지 못한다 - 셋 다 부호 없는 작은 부동소수다.
        R11G11B10Float,
        R16Float,
        R32Float,
        RG16Float,
        RGBA32Float
    };

    // 텍셀 하나의 바이트 수다. 올리기·되읽기가 행 하나의 길이를 이것으로 센다. 모르는 포맷은 0 이다.
    constexpr UInt32 GetTexelSize(TextureFormat format) noexcept
    {
        switch (format)
        {
        case TextureFormat::R16Float:
            return 2;
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::D32Float:
        case TextureFormat::R11G11B10Float:
        case TextureFormat::R32Float:
        case TextureFormat::RG16Float:
            return 4;
        case TextureFormat::RGBA16Float:
            return 8;
        case TextureFormat::RGBA32Float:
            return 16;
        case TextureFormat::Unknown:
            return 0;
        }
        return 0;
    }

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
        UByte4Norm,
        // 16 비트 넷을 0..1 로 읽는다. 스프라이트의 UV 사각형이 이 모양이다(D-114) - 8K 텍스처에서도 1/8 텍셀 정밀도다.
        UShort4Norm
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

    // `Alpha` 는 곧은 알파(색 SrcAlpha·InvSrcAlpha)다. `Layer*` 넷은 **미리 곱한 색**을 얹는다(D-279) - 투명하게 지운 텍스처에
    // `Alpha` 로 그린 레이어가 그렇다. 색에 알파를 또 곱하면 반투명한 가장자리가 어두워진다. 넷 모두 알파는 One·InvSrcAlpha 다.
    //   LayerNormal   색 One·InvSrcAlpha       s + (1 - sa)·d
    //   LayerAdditive 색 One·One               s + d
    //   LayerMultiply 색 DestColor·InvSrcAlpha s·d + (1 - sa)·d
    //   LayerScreen   색 InvDestColor·One      s·(1 - d) + d
    enum class BlendMode : std::uint8_t
    {
        Opaque,
        Alpha,
        LayerNormal,
        LayerAdditive,
        LayerMultiply,
        LayerScreen
    };

    enum class CullMode : std::uint8_t
    {
        None,
        Front,
        Back
    };

    // 픽셀로 세는 크기다. Core 의 `SizeU` 와 같은 것이라 이름만 다르게 둔다(D-249) -
    // 전에는 같은 모양을 RHI 가 따로 들고 있어서, 에셋과 에디터는 이것을 쓰지 못하고
    // `width`·`height` 를 낱개로 들고 다녔다.
    using Extent2D = SizeU;

    struct ClearColor
    {
        Float red = 0.0f;
        Float green = 0.0f;
        Float blue = 0.0f;
        Float alpha = 1.0f;
    };

    struct Viewport
    {
        Float x = 0.0f;
        Float y = 0.0f;
        Float width = 0.0f;
        Float height = 0.0f;
        Float minDepth = 0.0f;
        Float maxDepth = 1.0f;
    };

    struct ScissorRect
    {
        Int32 left = 0;
        Int32 top = 0;
        Int32 right = 0;
        Int32 bottom = 0;
    };

    struct BufferDesc
    {
        std::size_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryType memory = MemoryType::Device;
    };

    enum class TextureDimension : std::uint8_t
    {
        Texture2D,
        // 컬러 그레이딩 LUT 의 모양이다(postprocess-plan §2.1). 지금은 셰이더가 읽기만 한다 - `Sampled`·`CopyDestination` 만,
        // mip 하나만 받는다. `depthOrLayers` 가 깊이다.
        Texture3D
    };

    struct TextureDesc
    {
        Extent2D extent;
        // 2D 는 1 이다(배열은 아직 받지 않는다). 3D 는 깊이이고 `DeviceCaps::maxTexture3DSize` 까지다.
        UInt32 depthOrLayers = 1;
        UInt32 mipLevels = 1;
        UInt32 sampleCount = 1;
        TextureFormat format = TextureFormat::Unknown;
        TextureUsage usage = TextureUsage::None;
        TextureDimension dimension = TextureDimension::Texture2D;
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
        // 3D 텍스처의 깊이 축이다. 2D 텍스처는 보지 않는다.
        AddressMode addressW  = AddressMode::ClampToEdge;
    };

    struct ShaderBytecode
    {
        const void* data = nullptr;
        UInt32 size = 0;
    };

    struct VertexAttributeDesc
    {
        UInt32 shaderLocation = 0;
        UInt32 offset = 0;
        VertexFormat format = VertexFormat::Float2;
    };

    struct VertexBufferLayoutDesc
    {
        UInt32 stride = 0;
        VertexStepMode stepMode = VertexStepMode::Vertex;
        JArrayView<VertexAttributeDesc> attributes;
    };

    // 한 종류의 자원이 파이프라인에서 차지하는 자리다. `stages` 는 그 자원을 읽는 단계다 - 백엔드가 보이는 범위를
    // 거기까지만 연다. 비어 있으면(`count == 0`) 단계는 보지 않는다.
    struct BindingRange
    {
        UInt32 count = 0;
        ShaderStage stages = ShaderStage::Pixel;
    };

    // 파이프라인이 읽는 자원의 자리표다(postprocess-plan §2.1). 슬롯 번호는 셰이더의 레지스터 번호다.
    //   constantBuffers  b1..b(count). **b0 은 푸시 상수 자리라 비워 둔다** - D3D12 는 루트 상수, D3D11 은 파이프라인의
    //                    상수 버퍼, Vulkan 은 `[[vk::push_constant]]` 가 그 자리다. SPIR-V 에서는 b# 가 그대로 binding 이다.
    //   sampledTextures  t0..t(count-1). SPIR-V binding 8 부터.
    //   samplers         s0..s(count-1). SPIR-V binding 16 부터.
    // 상한은 `IRHIDevice::GetCaps` 가 말한다. 넘는 파이프라인은 만들어지지 않는다.
    //
    // **개수를 파이프라인이 미리 말해야 한다.** 백엔드가 이것으로 루트 시그니처·디스크립터 셋 레이아웃을 만들고,
    // 그것은 파이프라인과 함께 만들어져 바뀌지 않는다. 그리기 직전에 알 수 있는 값이 아니다.
    struct BindingLayoutDesc
    {
        BindingRange constantBuffers;
        BindingRange sampledTextures;
        BindingRange samplers;
    };

    // 푸시 상수(`SetGraphicsConstants`)의 상한이다. Vulkan 이 보장하는 최소가 128 B 라 세 백엔드가 이것을 지킨다.
    // 더 큰 것은 상수 버퍼로 올린다.
    inline constexpr UInt32 MaxPushConstantBytes = 128;

    // 텍스처 하나의 mip 수 상한이다(`DeviceCaps::maxTextureMipLevels`).
    inline constexpr UInt32 MaxTextureMipLevels = 16;

    // 지금 패스가 그리는 자리들이다. 백엔드의 컨텍스트가 `BeginRenderPass` 에서 적고 `EndRenderPass` 에서 비우며, 텍스처를 묶을 때
    // 겹치는지 묻는다(`SetTextureMip` 의 계약). 깊이는 mip 0 하나다.
    struct RenderPassTargets
    {
        static constexpr UInt32 Capacity = 9;
        TextureHandle textures[Capacity];
        UInt32 mips[Capacity] = {};
        UInt32 count = 0;

        void Clear() noexcept
        {
            count = 0;
        }

        void Add(TextureHandle texture, UInt32 mip) noexcept
        {
            if (count < Capacity)
            {
                textures[count] = texture;
                mips[count] = mip;
                ++count;
            }
        }

        // `mipLevel` 을 읽으면 이 패스가 그리는 자리와 겹치는지다. `allMips` 면 그 텍스처의 어느 mip 이든 겹친다.
        Bool Overlaps(TextureHandle texture, UInt32 mipLevel, Bool allMips) const noexcept
        {
            for (UInt32 index = 0; index < count; ++index)
            {
                if (textures[index] == texture && (allMips || mips[index] == mipLevel))
                {
                    return true;
                }
            }
            return false;
        }
    };

    // 한 변의 길이에서 mip 의 길이다. 1 아래로 내려가지 않는다.
    constexpr UInt32 GetMipExtent(UInt32 extent, UInt32 mipLevel) noexcept
    {
        const UInt32 shifted = mipLevel >= 32 ? UInt32(0) : UInt32(extent.Get() >> mipLevel.Get());
        return shifted == 0 ? UInt32(1) : shifted;
    }

    // 크기에 맞는 mip 수인지 본다 - 가장 긴 변이 1 이 되는 mip 까지가 끝이고, `MaxTextureMipLevels` 를 넘지 않는다.
    constexpr Bool IsMipCountValid(const Extent2D& extent, UInt32 mipLevels) noexcept
    {
        if (mipLevels == 0 || mipLevels > MaxTextureMipLevels)
        {
            return false;
        }
        const UInt32 longest = extent.width > extent.height ? extent.width : extent.height;
        UInt32 chain = 1;
        while (chain < 32 && (longest.Get() >> chain.Get()) != 0)
        {
            ++chain;
        }
        return mipLevels <= chain;
    }

    struct GraphicsPipelineDesc
    {
        ShaderBytecode vertexShader;
        ShaderBytecode pixelShader;
        JArrayView<VertexBufferLayoutDesc> vertexBuffers;
        JArrayView<TextureFormat> colorFormats;
        TextureFormat depthFormat = TextureFormat::Unknown;
        // 깊이 첨부가 있는 패스에 들어가는 파이프라인은 `depthFormat` 이 그 첨부와 같아야 한다 - 세 API 모두
        // 포맷을 파이프라인에 굽는다. 시험·쓰기는 그것과 별개다: 깊이가 달린 패스 위에 그리되 깊이를 보지도
        // 쓰지도 않는 것(2D 스프라이트가 3D 위에 얹힐 때)이 이 둘을 끈다. 포맷이 없으면 둘은 뜻이 없다.
        Bool depthTest = true;
        Bool depthWrite = true;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        BlendMode blend = BlendMode::Opaque;
        CullMode cull = CullMode::Back;
        ShaderStage pushConstantStages = ShaderStage::Vertex;
        // `MaxPushConstantBytes` 이하이고 4 의 배수다.
        UInt32 pushConstantBytes = 0;
        BindingLayoutDesc bindings;
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
        // 그릴 mip 이다(postprocess-plan §2.1 서브리소스). 패스의 기본 뷰포트·시저와 렌더 영역은 그 mip 의 크기다 -
        // 블룸의 내림 사슬이 mip 하나를 읽고 다음 mip 에 그린다. 백버퍼는 0 만 받는다.
        UInt32 mipLevel = 0;
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
        Float clearDepth = 1.0f;
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

        virtual Bool BeginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void EndRenderPass() = 0;
        virtual void SetViewport(const Viewport& viewport) = 0;
        virtual void SetScissor(const ScissorRect& scissor) = 0;
        virtual Bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;
        virtual Bool SetVertexBuffer(
            UInt32 slot,
            BufferHandle buffer,
            UInt32 stride,
            std::size_t offset) = 0;
        virtual Bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) = 0;
        virtual Bool SetGraphicsConstants(JArrayView<std::byte> data) = 0;
        // 슬롯에 텍스처와 샘플러를 묶는다. 슬롯 번호는 셰이더의 `t`/`s` 레지스터 번호다.
        // 파이프라인이 선언한 개수를 넘는 슬롯은 거절한다 — 루트 시그니처에 자리가 없다.
        //
        // 그리기 직전까지 기억만 하고, 실제 묶는 것은 드로우 호출에서 한 번에 한다.
        // 한 드로우에 필요한 것이 다 모인 뒤라야 디스크립터를 연속으로 놓을 수 있다.
        virtual Bool SetTexture(UInt32 slot, TextureHandle texture) = 0;
        // 텍스처의 mip 하나만 묶는다. 셰이더에게는 mip 이 하나뿐인 텍스처로 보인다(`SampleLevel(..., 0)`·`Load` 의 mip 0 이
        // 그 mip 이다). 같은 텍스처의 다른 mip 에 그리는 패스 안에서 읽을 수 있는 길이 이것이다 - 텍스처 전체를 묶으면 그리는
        // mip 까지 읽는 자리에 걸린다. 없는 mip 은 거절한다.
        //
        // **지금 패스가 그리는 mip(깊이 첨부면 그 깊이)은 `SetTexture`·`SetTextureMip` 둘 다 거절한다.** 같은 자리를 읽고 쓰면
        // 결과가 정의되지 않는다 - D3D12 는 검증 오류, D3D11 은 몰래 떼어 냄, Vulkan 은 레이아웃이 맞지 않는다.
        virtual Bool SetTextureMip(UInt32 slot, TextureHandle texture, UInt32 mipLevel) = 0;
        virtual Bool SetSampler(UInt32 slot, SamplerHandle sampler) = 0;
        // 슬롯에 상수 버퍼의 한 구간을 묶는다. 슬롯 번호는 셰이더의 `b` 레지스터 번호이고 1 부터다(b0 은 푸시 상수).
        // 버퍼는 `BufferUsage::Constant` 로 만든 것이다. `offset`·`size` 는 `DeviceCaps::constantBufferAlignment` 의 배수이고
        // `size` 는 `maxConstantBufferBytes` 이하이며 구간이 버퍼 안에 있어야 한다. 프레임마다 바뀌는 값은 `ConstantRing` 이
        // 이 모양으로 내준다. 파이프라인이 선언하지 않은 슬롯이나 조건이 맞지 않는 구간은 거절한다.
        //
        // 텍스처처럼 파이프라인을 바꾸면 버려지고, 선언한 슬롯이 하나라도 비어 있으면 그리지 않는다.
        virtual Bool SetConstantBuffer(UInt32 slot, BufferHandle buffer, std::size_t offset, std::size_t size) = 0;
        virtual Bool DrawIndexedInstanced(
            UInt32 indexCount,
            UInt32 instanceCount,
            UInt32 firstIndex,
            Int32 baseVertex,
            UInt32 firstInstance) = 0;
        // 인덱스 없이 정점 번호 순서대로 그린다. 정점 버퍼 없이 `SV_VertexID` 로 모양을 만드는 셰이더(전체 화면 삼각형,
        // `DrawInstanced(3, 1, 0, 0)`)가 이것을 쓴다.
        virtual Bool DrawInstanced(
            UInt32 vertexCount,
            UInt32 instanceCount,
            UInt32 firstVertex,
            UInt32 firstInstance) = 0;
        // **렌더 타깃 하나를 다른 렌더 타깃으로 통째로 복사한다**(D-283). 레이어 합성이 아래 그림을 읽는 블렌드(오버레이 등)에서 쓴다.
        // 둘은 같은 크기·같은 포맷이어야 하고 패스 밖에서만 부른다. 원본은 백버퍼여도 되고, 대상은 `RenderTarget` 으로 만든 텍스처다.
        // 상태(배리어·레이아웃)는 백엔드가 맞춘다 - `Sampled` 로 만든 대상은 다음 패스에서 곧바로 읽을 수 있다. 조건이 맞지 않으면 거짓이고
        // 아무것도 기록하지 않는다.
        virtual Bool CopyTexture(TextureHandle source, TextureHandle destination) = 0;
    };

    struct FrameContext
    {
        UInt64 serial = 0;
        UInt32 slot = 0;
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
        Bool enableValidation = false;
    };

    // 장치가 지키는 상한이다. 파이프라인과 묶기가 이것을 넘으면 거절된다. 세 백엔드의 공통 바닥을 기본으로 두고,
    // 백엔드는 그 장치가 실제로 못 하는 것만 낮춘다.
    struct DeviceCaps
    {
        // 파이프라인 하나가 선언할 수 있는 수다. 상수 버퍼는 b1..b7 이다 - Vulkan 에서 binding 8 부터는 텍스처 자리다.
        UInt32 maxConstantBuffers = 7;
        UInt32 maxSampledTextures = 8;
        UInt32 maxSamplers = 4;
        UInt32 maxPushConstantBytes = MaxPushConstantBytes;
        // `SetConstantBuffer` 의 오프셋과 크기의 단위다. D3D12 의 CBV 와 D3D11.1 의 부분 묶기가 256 B 를 요구한다.
        UInt32 constantBufferAlignment = 256;
        // `SetConstantBuffer` 한 번에 묶을 수 있는 크기다. D3D 는 64 KiB 이고 Vulkan 은 장치의 `maxUniformBufferRange` 다.
        UInt32 maxConstantBufferBytes = 65536;
        // 오프셋이 0 이 아닌 구간을 묶을 수 있다. D3D11 은 11.1 의 부분 묶기와 동적 상수 버퍼의 NO_OVERWRITE 를 모두 지원할
        // 때만 참이다. 거짓이면 `ConstantRing` 을 만들 수 없다.
        Bool constantBufferOffsets = true;
        // 텍스처 하나의 mip 수 상한이다. 16 이면 한 변 32768 까지의 사슬 전부다. 백엔드가 mip 마다 뷰를 미리 만들어 두므로
        // (그리기 경로에서 뷰를 만들지 않는다) 상한이 있다.
        UInt32 maxTextureMipLevels = MaxTextureMipLevels;
        // 3D 텍스처 한 변의 상한이다. D3D 는 2048 이고 Vulkan 은 장치의 `maxImageDimension3D` 다(명세의 최소는 256).
        UInt32 maxTexture3DSize = 2048;
    };

    // 텍스처의 차원 규칙이다. 세 백엔드가 같은 것을 거절하도록 한 자리에 둔다(포맷·쓰임의 나머지 검사는 백엔드가 한다).
    //   2D  `depthOrLayers == 1` - 배열은 아직 받지 않는다. mip 은 `IsMipCountValid`.
    //   3D  셰이더가 읽기만 한다(`Sampled`·`CopySource`·`CopyDestination`), mip 하나, 세 변 모두 `maxTexture3DSize` 까지, 깊이 포맷은 안 된다.
    constexpr Bool IsTextureDimensionValid(const TextureDesc& desc, const DeviceCaps& caps) noexcept
    {
        if (desc.dimension == TextureDimension::Texture2D)
        {
            return desc.depthOrLayers == 1 && IsMipCountValid(desc.extent, desc.mipLevels);
        }
        constexpr std::uint32_t readOnly = static_cast<std::uint32_t>(TextureUsage::Sampled)
            | static_cast<std::uint32_t>(TextureUsage::CopySource) | static_cast<std::uint32_t>(TextureUsage::CopyDestination);
        return desc.dimension == TextureDimension::Texture3D
            && desc.depthOrLayers >= 1 && desc.depthOrLayers <= caps.maxTexture3DSize
            && desc.extent.width <= caps.maxTexture3DSize && desc.extent.height <= caps.maxTexture3DSize
            && desc.mipLevels == 1
            && (static_cast<std::uint32_t>(desc.usage) & ~readOnly) == 0
            && desc.format != TextureFormat::D32Float;
    }

    // 자리표가 장치의 상한 안이고, 쓰는 종류마다 읽는 단계를 말했는지 본다. 백엔드가 파이프라인을 만들기 전에 부른다.
    constexpr Bool IsBindingLayoutValid(const BindingLayoutDesc& layout, const DeviceCaps& caps) noexcept
    {
        constexpr std::uint8_t knownStages =
            static_cast<std::uint8_t>(ShaderStage::Vertex) | static_cast<std::uint8_t>(ShaderStage::Pixel);
        const BindingRange ranges[] = {layout.constantBuffers, layout.sampledTextures, layout.samplers};
        const UInt32 limits[] = {caps.maxConstantBuffers, caps.maxSampledTextures, caps.maxSamplers};
        for (UInt32 index = 0; index < 3; ++index)
        {
            const std::uint8_t stages = static_cast<std::uint8_t>(ranges[index].stages);
            if (ranges[index].count > limits[index])
            {
                return false;
            }
            if (ranges[index].count != 0 && (stages == 0 || (stages & ~knownStages) != 0))
            {
                return false;
            }
        }
        return true;
    }

    // 읽어 온 이미지의 모양이다. 목적지에는 행 패딩 없이 빽빽하게 쓴다 —
    // GPU 쪽 행 정렬은 백엔드의 사정이고 부르는 쪽이 알 필요가 없다.
    struct TextureReadback
    {
        Extent2D      extent;
        TextureFormat format = TextureFormat::Unknown;
        UInt32 rowPitch = 0;
        UInt32 writtenBytes = 0;
    };

    class IRHIDevice
    {
    public:
        virtual ~IRHIDevice() = default;

        virtual DeviceCaps GetCaps() const = 0;
        virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        // `Upload` 메모리 버퍼에 쓴다. 기다리지 않는다 - GPU 가 아직 읽는 자리를 덮지 않는 것은 부르는 쪽의 몫이다.
        //
        // **`Upload` 상수 버퍼는 링으로만 쓴다.** 오프셋 0 쓰기가 새 바퀴를 열고, 그 앞에 쓴 내용은 남지 않을 수 있다
        // (D3D11 은 그때 버퍼를 갈아 끼운다 - DISCARD). 한 바퀴 안에서는 이미 쓴 구간을 다시 쓰지 않는다(NO_OVERWRITE).
        // `ConstantRing` 이 이 규칙대로 쓴다.
        virtual Bool WriteBuffer(
            BufferHandle buffer,
            std::size_t offset,
            JArrayView<std::byte> data) = 0;
        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        virtual void DestroyTexture(TextureHandle texture) = 0;
        // 텍스처 한 면에 픽셀을 올린다. 바이트는 행 패딩 없이 빽빽한 것으로 받는다 —
        // GPU 쪽 행 정렬은 백엔드의 사정이다(`ReadTexture` 와 같은 계약이다).
        // 3D 텍스처는 깊이 0 부터의 조각을 차례로 이어 준다(조각 하나 = 행 × 높이). `WriteTextureRegion`·`ReadTexture` 는 3D 를 받지 않는다.
        //
        // **GPU 가 끝날 때까지 기다린다.** 로드 경로이고 프레임 안에서는 거절한다.
        virtual Bool WriteTexture(
            TextureHandle texture,
            UInt32 mipLevel,
            JArrayView<std::byte> data) = 0;
        // 텍스처 한 면의 **사각형 하나**만 올린다(글리프 아틀라스의 새 칸, text-plan §3.6). data 는 사각형 왼쪽 위 텍셀부터이고
        // 행 간격은 rowPitch 바이트다 - 큰 CPU 페이지의 한 조각을 옮겨 담지 않고 그대로 넘길 수 있다. 사각형이 텍스처 밖으로 나가면
        // 거절한다. `WriteTexture` 처럼 GPU 를 기다리고 프레임 안에서는 거절한다. 구현하지 않은 백엔드는 거짓이고, 부르는 쪽은 그때
        // 전체를 올린다.
        virtual Bool WriteTextureRegion(
            TextureHandle texture,
            UInt32 mipLevel,
            UInt32 x,
            UInt32 y,
            UInt32 width,
            UInt32 height,
            JArrayView<std::byte> data,
            UInt32 rowPitch)
        {
            (void)texture;
            (void)mipLevel;
            (void)x;
            (void)y;
            (void)width;
            (void)height;
            (void)data;
            (void)rowPitch;
            return false;
        }
        virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
        virtual void DestroySampler(SamplerHandle sampler) = 0;
        virtual GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        virtual void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;

        virtual SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) = 0;
        virtual void DestroySwapchain(SwapchainHandle swapchain) = 0;
        virtual Bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) = 0;

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
        virtual Bool ReadTexture(
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

        // 동시에 떠 있을 수 있는 프레임 수다.
        //
        // **CPU 가 매 프레임 덮어쓰는 자원은 이만큼 나눠 가져야 한다.** 하나로
        // 두면 지난 프레임이 아직 읽는 중에 다음 프레임이 덮어쓴다 -
        // `WriteBuffer` 는 매핑된 메모리에 그냥 memcpy 이고 기다려 주지 않는다.
        // 나누는 자리는 프레임 슬롯이다(`FrameContext::slot`). 그 슬롯의 지난
        // 프레임이 끝났다는 것은 `BeginFrame` 이 이미 보장한다.
        virtual UInt32 GetFramesInFlight() const
        {
            return 1;
        }

        // 그래픽 API 의 검증 레이어가 남긴 오류·손상 메시지의 수다.
        //
        // **이것은 테스트가 붙잡는 손잡이다.** D3D12 는 잘못된 리소스 상태나 잘못된
        // 시저 같은 것을 대개 조용히 지나간다 - 그 기계의 드라이버가 알아서 주워 담고,
        // 그림이 맞게 나오니 테스트도 통과한다. 그러고는 다른 기계에서 깨진다.
        // 픽셀만 보아서는 그 차이를 볼 수 없고, 검증 레이어만이 말해 준다.
        //
        // 검증을 켜지 않았거나 백엔드가 그런 것을 갖고 있지 않으면 0 이다.
        virtual UInt32 GetValidationErrorCount() const
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
