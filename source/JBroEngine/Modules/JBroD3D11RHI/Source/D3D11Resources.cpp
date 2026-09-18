#include "D3D11Device.h"

#include <cstring>

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
        case TextureFormat::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    std::uint32_t PixelSize(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::D32Float:
            return 4;
        case TextureFormat::RGBA16Float:
            return 8;
        case TextureFormat::Unknown:
            return 0;
        }
        return 0;
    }

    namespace
    {
        bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }

        bool HasTextureUsage(TextureUsage usages, TextureUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }

        constexpr std::uint32_t TextureUsageMask =
            static_cast<std::uint32_t>(TextureUsage::Sampled)
            | static_cast<std::uint32_t>(TextureUsage::RenderTarget)
            | static_cast<std::uint32_t>(TextureUsage::DepthStencil)
            | static_cast<std::uint32_t>(TextureUsage::Storage)
            | static_cast<std::uint32_t>(TextureUsage::CopySource)
            | static_cast<std::uint32_t>(TextureUsage::CopyDestination);

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
        std::uint32_t index = MaxBuffers;
        for (std::uint32_t at = 0; at < MaxBuffers; ++at)
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
        const bool constant = HasBufferUsage(desc.usage, BufferUsage::Constant);
        native.ByteWidth = constant ? RoundUpTo16(desc.size) : static_cast<UINT>(desc.size);
        // 전부 DEFAULT 다. 쓰기는 `UpdateSubresource` 로 간다 - 드라이버가 이름을 바꿔 준다.
        native.Usage = D3D11_USAGE_DEFAULT;
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
        const std::uint32_t generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    bool D3D11Device::WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data)
    {
        ID3D11Buffer* native = nullptr;
        BufferDesc desc;
        if (m_context == nullptr || data.data == nullptr || data.size == 0
            || false == ResolveBuffer(buffer, native, desc)
            || offset > desc.size || data.size > desc.size - offset)
        {
            return false;
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

    bool D3D11Device::ResolveBuffer(BufferHandle buffer, ID3D11Buffer*& native, BufferDesc& desc)
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
        const std::uint32_t usages = static_cast<std::uint32_t>(desc.usage);
        const bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
        const bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive
            || desc.extent.width == 0 || desc.extent.height == 0
            || desc.depthOrLayers == 0 || desc.mipLevels == 0 || desc.sampleCount != 1
            || desc.format == TextureFormat::Unknown
            || usages == 0 || (usages & ~TextureUsageMask) != 0
            || (renderTarget && depthStencil)
            || (depthStencil && desc.format != TextureFormat::D32Float)
            || (false == depthStencil && desc.format == TextureFormat::D32Float))
        {
            return {};
        }
        std::uint32_t slot = MaxTextures;
        for (std::uint32_t at = 0; at < MaxTextures; ++at)
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
        native.Format = ToNativeFormat(desc.format);
        native.SampleDesc.Count = 1;
        native.Usage = D3D11_USAGE_DEFAULT;
        if (HasTextureUsage(desc.usage, TextureUsage::Sampled))
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
        bool viewsMade = true;
        if (renderTarget)
        {
            viewsMade = SUCCEEDED(m_device->CreateRenderTargetView(state.texture.Get(), nullptr, &state.renderTargetView));
        }
        if (viewsMade && depthStencil)
        {
            viewsMade = SUCCEEDED(m_device->CreateDepthStencilView(state.texture.Get(), nullptr, &state.depthStencilView));
        }
        if (viewsMade && HasTextureUsage(desc.usage, TextureUsage::Sampled))
        {
            viewsMade = SUCCEEDED(m_device->CreateShaderResourceView(state.texture.Get(), nullptr, &state.shaderResourceView));
        }
        if (false == viewsMade)
        {
            const std::uint32_t generation = state.generation;
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
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return;
        }
        const std::uint32_t generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    bool D3D11Device::WriteTexture(TextureHandle texture, std::uint32_t mipLevel, JArrayView<std::byte> data)
    {
        if (m_context == nullptr || m_frameActive || false == texture.IsValid() || texture.index < TextureResourceBase
            || data.data == nullptr || data.size == 0)
        {
            return false;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
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
        const std::uint32_t width = (std::max)(1u, state.desc.extent.width >> mipLevel);
        const std::uint32_t height = (std::max)(1u, state.desc.extent.height >> mipLevel);
        const std::uint32_t pixelSize = PixelSize(state.desc.format);
        const std::size_t rowPitch = static_cast<std::size_t>(width) * pixelSize;
        if (pixelSize == 0 || data.size != rowPitch * height)
        {
            return false;
        }
        m_context->UpdateSubresource(state.texture.Get(), mipLevel, nullptr, data.data,
            static_cast<UINT>(rowPitch), 0);
        return true;
    }

    bool D3D11Device::ResolveRenderTargetView(TextureHandle texture, ID3D11RenderTargetView*& view)
    {
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }
        if (texture.index < TextureResourceBase)
        {
            const std::uint32_t index = texture.index - BackBufferTextureBase;
            if (index >= MaxSwapchains)
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
            return true;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation || state.renderTargetView == nullptr)
        {
            return false;
        }
        view = state.renderTargetView.Get();
        return true;
    }

    bool D3D11Device::ResolveDepthStencilView(TextureHandle texture, ID3D11DepthStencilView*& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase)
        {
            return false;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
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

    bool D3D11Device::ResolveShaderResourceView(TextureHandle texture, ID3D11ShaderResourceView*& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase)
        {
            return false;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
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
        return true;
    }

    // ── 샘플러 ──────────────────────────────────────────────────────────────

    SamplerHandle D3D11Device::CreateSampler(const SamplerDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive)
        {
            return {};
        }
        std::uint32_t index = MaxSamplers;
        for (std::uint32_t at = 0; at < MaxSamplers; ++at)
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
        const bool minLinear = desc.minFilter == FilterMode::Linear;
        const bool magLinear = desc.magFilter == FilterMode::Linear;
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
        const std::uint32_t generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    bool D3D11Device::ResolveSampler(SamplerHandle sampler, ID3D11SamplerState*& native)
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
