#include "D3D11Device.h"

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Internal
{
    DXGI_FORMAT ToNativeFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8UnormSrgb:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8Unorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8UnormSrgb:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case TextureFormat::RGBA16Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::D32Float:
            return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::R11G11B10Float:
            return DXGI_FORMAT_R11G11B10_FLOAT;
        case TextureFormat::R16Float:
            return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::R32Float:
            return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RG16Float:
            return DXGI_FORMAT_R16G16_FLOAT;
        case TextureFormat::RGBA32Float:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    UInt32 PixelSize(TextureFormat format)
    {
        return GetTexelSize(format);
    }

    namespace
    {
        Bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<JBro::UInt32>(usages) & static_cast<JBro::UInt32>(usage)) != 0;
        }

        Bool HasTextureUsage(TextureUsage usages, TextureUsage usage)
        {
            return (static_cast<JBro::UInt32>(usages) & static_cast<JBro::UInt32>(usage)) != 0;
        }

        constexpr UInt32 TextureUsageMask =
            static_cast<JBro::UInt32>(TextureUsage::Sampled)
            | static_cast<JBro::UInt32>(TextureUsage::RenderTarget)
            | static_cast<JBro::UInt32>(TextureUsage::DepthStencil)
            | static_cast<JBro::UInt32>(TextureUsage::Storage)
            | static_cast<JBro::UInt32>(TextureUsage::CopySource)
            | static_cast<JBro::UInt32>(TextureUsage::CopyDestination);

        UINT RoundUpTo16(std::size_t size)
        {
            return static_cast<UINT>((size + 15) & ~static_cast<std::size_t>(15));
        }
    }

    // ── 버퍼 ────────────────────────────────────────────────────────────────

    BufferHandle D3D11Device::CreateBuffer(const BufferDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive
            || desc.size == 0 || desc.usage == BufferUsage::None
            || desc.size > static_cast<std::size_t>(D3D11_REQ_RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_A_TERM) * 1024 * 1024)
        {
            return {};
        }
        UInt32 index = MaxBuffers;
        for (UInt32 at = 0; at < MaxBuffers; ++at)
        {
            if (false == m_buffers[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxBuffers)
        {
            return {};
        }
        D3D11_BUFFER_DESC native = {};
        const Bool constant = HasBufferUsage(desc.usage, BufferUsage::Constant);
        const Bool constantRing = constant && desc.memory == MemoryType::Upload;
        if (constantRing
            && (HasBufferUsage(desc.usage, BufferUsage::Vertex) || HasBufferUsage(desc.usage, BufferUsage::Index)))
        {
            // 동적 상수 버퍼는 다른 묶기와 함께 둘 수 없다(D3D11 의 제약).
            return {};
        }
        native.ByteWidth = constant ? RoundUpTo16(desc.size) : static_cast<UINT>(desc.size);
        // 대부분 DEFAULT 다. 쓰기는 `UpdateSubresource` 로 간다 - 드라이버가 이름을 바꿔 준다.
        // `Upload` 상수 버퍼만 DYNAMIC 이다. 한 프레임 안에 구간을 여럿 써서 각각 묶는 링이라(`WriteBuffer` 의 계약)
        // 통째로 갈아 쓰는 `UpdateSubresource` 로는 앞서 묶은 구간이 덮인다.
        native.Usage = constantRing ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        native.CPUAccessFlags = constantRing ? D3D11_CPU_ACCESS_WRITE : 0;
        if (HasBufferUsage(desc.usage, BufferUsage::Vertex))
        {
            native.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        }
        if (HasBufferUsage(desc.usage, BufferUsage::Index))
        {
            native.BindFlags |= D3D11_BIND_INDEX_BUFFER;
        }
        if (constant)
        {
            native.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
        }
        D3D11BufferState& state = m_buffers[index];
        if (FAILED(m_device->CreateBuffer(&native, nullptr, &state.buffer)))
        {
            state.buffer.Reset();
            return {};
        }
        state.desc = desc;
        state.occupied = true;
        return BufferHandle{index, state.generation};
    }

    void D3D11Device::DestroyBuffer(BufferHandle buffer)
    {
        if (buffer.index >= MaxBuffers)
        {
            return;
        }
        D3D11BufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return;
        }
        // 컨텍스트가 아직 쓰고 있어도 참조 계수가 지킨다. 바로 놓는다.
        const UInt32 generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    Bool D3D11Device::WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data)
    {
        ID3D11Buffer* native = nullptr;
        BufferDesc desc;
        if (m_context == nullptr || data.data == nullptr || data.size == 0
            || false == ResolveBuffer(buffer, native, desc)
            || offset > desc.size || data.size > desc.size - offset)
        {
            return false;
        }
        if (HasBufferUsage(desc.usage, BufferUsage::Constant) && desc.memory == MemoryType::Upload)
        {
            // 링이다. 오프셋 0 이 새 바퀴를 열며 버퍼를 갈아 끼우고(DISCARD), 그 뒤의 구간은 앞서 묶은 구간을 건드리지
            // 않는다(NO_OVERWRITE). 부분 쓰기는 11.1 기능이라 없는 기계에서는 통째로 쓰는 0 만 받는다.
            const Bool rewind = offset == 0;
            if (false == rewind && false == m_constantBufferOffsets)
            {
                return false;
            }
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(m_context->Map(native, 0, rewind ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0,
                    &mapped))
                || mapped.pData == nullptr)
            {
                return false;
            }
            std::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data.data, data.size);
            m_context->Unmap(native, 0);
            return true;
        }
        if (HasBufferUsage(desc.usage, BufferUsage::Constant))
        {
            // 상수 버퍼는 **통째로만** 쓴다(D3D11 은 상수 버퍼의 일부만 갱신할 수 없다). 일부만 주면 나머지가
            // 0 이 되어 D3D12 와 다르게 행동하므로 거절한다. 16 단위 꼬리만 채운 사본으로 간다.
            if (offset != 0 || data.size != desc.size)
            {
                return false;
            }
            const UINT padded = RoundUpTo16(desc.size);
            if (padded > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16)
            {
                return false;
            }
            if (padded == data.size)
            {
                m_context->UpdateSubresource(native, 0, nullptr, data.data, 0, 0);
                return true;
            }
            unsigned char copy[D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16];
            std::memcpy(copy, data.data, data.size);
            std::memset(copy + data.size, 0, padded - data.size);
            m_context->UpdateSubresource(native, 0, nullptr, copy, 0, 0);
            return true;
        }
        D3D11_BOX box = {};
        box.left = static_cast<UINT>(offset);
        box.right = static_cast<UINT>(offset + data.size);
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        m_context->UpdateSubresource(native, 0, &box, data.data, 0, 0);
        return true;
    }

    Bool D3D11Device::ResolveBuffer(BufferHandle buffer, ID3D11Buffer*& native, BufferDesc& desc)
    {
        if (buffer.index >= MaxBuffers)
        {
            return false;
        }
        D3D11BufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return false;
        }
        native = state.buffer.Get();
        desc = state.desc;
        return true;
    }

    // ── 텍스처 ──────────────────────────────────────────────────────────────

    TextureHandle D3D11Device::CreateTexture(const TextureDesc& desc)
    {
        const UInt32 usages = static_cast<JBro::UInt32>(desc.usage);
        const Bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
        const Bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive
            || desc.extent.width == 0 || desc.extent.height == 0
            // 배열과 3D 는 아직 받지 않는다(D3D12 와 같은 계약).
            || desc.depthOrLayers != 1 || false == IsMipCountValid(desc.extent, desc.mipLevels) || desc.sampleCount != 1
            || desc.format == TextureFormat::Unknown
            || usages == 0 || (usages & ~TextureUsageMask) != 0
            || (renderTarget && depthStencil)
            || (depthStencil && desc.format != TextureFormat::D32Float)
            || (depthStencil && desc.mipLevels != 1)
            || (false == depthStencil && desc.format == TextureFormat::D32Float))
        {
            return {};
        }
        const Bool sampled = HasTextureUsage(desc.usage, TextureUsage::Sampled);
        UInt32 slot = MaxTextures;
        for (UInt32 at = 0; at < MaxTextures; ++at)
        {
            if (false == m_textures[at].occupied)
            {
                slot = at;
                break;
            }
        }
        if (slot == MaxTextures)
        {
            return {};
        }
        D3D11_TEXTURE2D_DESC native = {};
        native.Width = desc.extent.width;
        native.Height = desc.extent.height;
        native.MipLevels = desc.mipLevels;
        native.ArraySize = desc.depthOrLayers;
        // 읽기도 하는 깊이는 typeless 로 만들고 뷰마다 포맷을 준다(DSV 는 D32, SRV 는 R32). D32_FLOAT 그대로는 셰이더 자원으로
        // 묶는 텍스처를 만들 수 없다 - 깊이 읽기가 D3D11 에서 막혔던 까닭이 이것이다(postprocess-plan §1).
        native.Format = depthStencil && sampled ? DXGI_FORMAT_R32_TYPELESS : ToNativeFormat(desc.format);
        native.SampleDesc.Count = 1;
        native.Usage = D3D11_USAGE_DEFAULT;
        if (sampled)
        {
            native.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
        if (renderTarget)
        {
            native.BindFlags |= D3D11_BIND_RENDER_TARGET;
        }
        if (depthStencil)
        {
            native.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        }
        D3D11TextureState& state = m_textures[slot];
        if (FAILED(m_device->CreateTexture2D(&native, nullptr, &state.texture)))
        {
            state.texture.Reset();
            return {};
        }
        Bool viewsMade = true;
        const DXGI_FORMAT viewFormat = depthStencil ? DXGI_FORMAT_R32_FLOAT : ToNativeFormat(desc.format);
        for (UInt32 mip = 0; renderTarget && viewsMade && mip < desc.mipLevels; ++mip)
        {
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
            viewDesc.Format = viewFormat;
            viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = mip;
            viewsMade = SUCCEEDED(m_device->CreateRenderTargetView(state.texture.Get(), &viewDesc, &state.renderTargetViews[mip]));
        }
        if (viewsMade && depthStencil)
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
            viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
            viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            viewsMade = SUCCEEDED(m_device->CreateDepthStencilView(state.texture.Get(), &viewDesc, &state.depthStencilView));
        }
        if (viewsMade && sampled)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
            viewDesc.Format = viewFormat;
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipLevels = desc.mipLevels;
            viewsMade = SUCCEEDED(m_device->CreateShaderResourceView(state.texture.Get(), &viewDesc, &state.shaderResourceView));
            for (UInt32 mip = 0; viewsMade && desc.mipLevels > 1 && mip < desc.mipLevels; ++mip)
            {
                viewDesc.Texture2D.MostDetailedMip = mip;
                viewDesc.Texture2D.MipLevels = 1;
                viewsMade = SUCCEEDED(
                    m_device->CreateShaderResourceView(state.texture.Get(), &viewDesc, &state.mipShaderResourceViews[mip]));
            }
        }
        if (false == viewsMade)
        {
            const UInt32 generation = state.generation;
            state = {};
            state.generation = generation;
            return {};
        }
        state.desc = desc;
        state.occupied = true;
        return TextureHandle{TextureResourceBase + slot, state.generation};
    }

    void D3D11Device::DestroyTexture(TextureHandle texture)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase)
        {
            return;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return;
        }
        const UInt32 generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    Bool D3D11Device::WriteTexture(TextureHandle texture, UInt32 mipLevel, JArrayView<std::byte> data)
    {
        if (m_context == nullptr || m_frameActive || false == texture.IsValid() || texture.index < TextureResourceBase
            || data.data == nullptr || data.size == 0)
        {
            return false;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation
            || mipLevel >= state.desc.mipLevels || state.desc.depthOrLayers != 1)
        {
            return false;
        }
        const UInt32 width = (std::max)(1u, state.desc.extent.width >> mipLevel);
        const UInt32 height = (std::max)(1u, state.desc.extent.height >> mipLevel);
        const UInt32 pixelSize = PixelSize(state.desc.format);
        const std::size_t rowPitch = static_cast<std::size_t>(width) * pixelSize;
        if (pixelSize == 0 || data.size != rowPitch * height)
        {
            return false;
        }
        m_context->UpdateSubresource(state.texture.Get(), mipLevel, nullptr, data.data,
            static_cast<UINT>(rowPitch), 0);
        return true;
    }

    Bool D3D11Device::WriteTextureRegion(TextureHandle texture, UInt32 mipLevel, UInt32 x, UInt32 y,
        UInt32 width, UInt32 height, JArrayView<std::byte> data, UInt32 rowPitch)
    {
        if (m_context == nullptr || m_frameActive || false == texture.IsValid() || texture.index < TextureResourceBase
            || data.data == nullptr || width == 0 || height == 0)
        {
            return false;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation
            || mipLevel >= state.desc.mipLevels || state.desc.depthOrLayers != 1)
        {
            return false;
        }
        const UInt32 levelWidth = (std::max)(1u, state.desc.extent.width >> mipLevel);
        const UInt32 levelHeight = (std::max)(1u, state.desc.extent.height >> mipLevel);
        const UInt32 pixelSize = PixelSize(state.desc.format);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * pixelSize;
        if (pixelSize == 0 || x + width > levelWidth || y + height > levelHeight || rowPitch < rowBytes
            || data.size < static_cast<std::size_t>(rowPitch) * (height - 1) + rowBytes)
        {
            return false;
        }
        D3D11_BOX box = {};
        box.left = x;
        box.top = y;
        box.front = 0;
        box.right = x + width;
        box.bottom = y + height;
        box.back = 1;
        m_context->UpdateSubresource(state.texture.Get(), mipLevel, &box, data.data, rowPitch, 0);
        return true;
    }

    Bool D3D11Device::ResolveRenderTargetView(TextureHandle texture, UInt32 mipLevel, ID3D11RenderTargetView*& view,
        Extent2D& extent)
    {
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }
        if (texture.index < TextureResourceBase)
        {
            const UInt32 index = texture.index - BackBufferTextureBase;
            if (index >= MaxSwapchains || mipLevel != 0)
            {
                return false;
            }
            D3D11SwapchainState& state = m_swapchains[index];
            if (false == state.occupied || state.backBufferGeneration != texture.generation
                || state.backBufferView == nullptr)
            {
                return false;
            }
            view = state.backBufferView.Get();
            extent = state.desc.extent;
            return true;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation || mipLevel >= state.desc.mipLevels
            || state.renderTargetViews[mipLevel] == nullptr)
        {
            return false;
        }
        view = state.renderTargetViews[mipLevel].Get();
        extent = {GetMipExtent(state.desc.extent.width, mipLevel), GetMipExtent(state.desc.extent.height, mipLevel)};
        return true;
    }

    Bool D3D11Device::ResolveDepthStencilView(TextureHandle texture, ID3D11DepthStencilView*& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase)
        {
            return false;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation || state.depthStencilView == nullptr)
        {
            return false;
        }
        view = state.depthStencilView.Get();
        return true;
    }

    Bool D3D11Device::ResolveShaderResourceView(TextureHandle texture, UInt32 mipLevel, ID3D11ShaderResourceView*& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase)
        {
            return false;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation || state.shaderResourceView == nullptr)
        {
            return false;
        }
        view = state.shaderResourceView.Get();
        if (mipLevel == AllMips)
        {
            return true;
        }
        if (mipLevel >= state.desc.mipLevels)
        {
            return false;
        }
        if (state.desc.mipLevels > 1)
        {
            view = state.mipShaderResourceViews[mipLevel].Get();
        }
        return view != nullptr;
    }

    // ── 샘플러 ──────────────────────────────────────────────────────────────

    SamplerHandle D3D11Device::CreateSampler(const SamplerDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive)
        {
            return {};
        }
        UInt32 index = MaxSamplers;
        for (UInt32 at = 0; at < MaxSamplers; ++at)
        {
            if (false == m_samplers[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxSamplers)
        {
            return {};
        }
        D3D11_SAMPLER_DESC native = {};
        const Bool minLinear = desc.minFilter == FilterMode::Linear;
        const Bool magLinear = desc.magFilter == FilterMode::Linear;
        native.Filter = minLinear
            ? (magLinear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR)
            : (magLinear ? D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT);
        native.AddressU = desc.addressU == AddressMode::Repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        native.AddressV = desc.addressV == AddressMode::Repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        native.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        native.MaxLOD = D3D11_FLOAT32_MAX;
        native.ComparisonFunc = D3D11_COMPARISON_NEVER;
        D3D11SamplerState& state = m_samplers[index];
        if (FAILED(m_device->CreateSamplerState(&native, &state.sampler)))
        {
            state.sampler.Reset();
            return {};
        }
        state.desc = desc;
        state.occupied = true;
        return SamplerHandle{index, state.generation};
    }

    void D3D11Device::DestroySampler(SamplerHandle sampler)
    {
        if (sampler.index >= MaxSamplers)
        {
            return;
        }
        D3D11SamplerState& state = m_samplers[sampler.index];
        if (false == state.occupied || state.generation != sampler.generation)
        {
            return;
        }
        const UInt32 generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    Bool D3D11Device::ResolveSampler(SamplerHandle sampler, ID3D11SamplerState*& native)
    {
        if (sampler.index >= MaxSamplers)
        {
            return false;
        }
        D3D11SamplerState& state = m_samplers[sampler.index];
        if (false == state.occupied || state.generation != sampler.generation)
        {
            return false;
        }
        native = state.sampler.Get();
        return true;
    }
}
