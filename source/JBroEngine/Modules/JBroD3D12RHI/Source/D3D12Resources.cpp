#include "D3D12Device.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace JBro::Internal
{
    namespace
    {
        constexpr std::uint32_t BufferUsageMask =
            static_cast<std::uint32_t>(BufferUsage::Vertex)
            | static_cast<std::uint32_t>(BufferUsage::Index)
            | static_cast<std::uint32_t>(BufferUsage::Constant)
            | static_cast<std::uint32_t>(BufferUsage::CopySource)
            | static_cast<std::uint32_t>(BufferUsage::CopyDestination);

        constexpr std::uint32_t TextureUsageMask =
            static_cast<std::uint32_t>(TextureUsage::Sampled)
            | static_cast<std::uint32_t>(TextureUsage::RenderTarget)
            | static_cast<std::uint32_t>(TextureUsage::DepthStencil)
            | static_cast<std::uint32_t>(TextureUsage::Storage)
            | static_cast<std::uint32_t>(TextureUsage::CopySource)
            | static_cast<std::uint32_t>(TextureUsage::CopyDestination);

        bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }

        bool HasTextureUsage(TextureUsage usages, TextureUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }

        std::uint32_t NextGeneration(std::uint32_t generation)
        {
            ++generation;
            if (generation == 0)
            {
                generation = 1;
            }
            return generation;
        }

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

        // 읽기 경로가 지원하는 포맷의 픽셀 크기다. 0 이면 지원하지 않는 포맷이다.
        std::uint32_t ReadbackPixelSize(TextureFormat format)
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
                default:
                    return 0;
            }
        }

        bool IsSrgbFormat(TextureFormat format)
        {
            return format == TextureFormat::RGBA8UnormSrgb
                || format == TextureFormat::BGRA8UnormSrgb;
        }

        D3D12_HEAP_PROPERTIES BuildHeapProperties(MemoryType memory)
        {
            D3D12_HEAP_PROPERTIES properties = {};
            switch (memory)
            {
            case MemoryType::Device:
                properties.Type = D3D12_HEAP_TYPE_DEFAULT;
                break;
            case MemoryType::Upload:
                properties.Type = D3D12_HEAP_TYPE_UPLOAD;
                break;
            case MemoryType::Readback:
                properties.Type = D3D12_HEAP_TYPE_READBACK;
                break;
            }
            properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            properties.CreationNodeMask = 1;
            properties.VisibleNodeMask = 1;
            return properties;
        }

        D3D12_RESOURCE_STATES InitialBufferState(MemoryType memory)
        {
            switch (memory)
            {
            case MemoryType::Device:
                return D3D12_RESOURCE_STATE_COMMON;
            case MemoryType::Upload:
                return D3D12_RESOURCE_STATE_GENERIC_READ;
            case MemoryType::Readback:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            }
            return D3D12_RESOURCE_STATE_COMMON;
        }

        bool IsBufferDescValid(const BufferDesc& desc)
        {
            const std::uint32_t usages = static_cast<std::uint32_t>(desc.usage);
            if (desc.size == 0 || usages == 0 || (usages & ~BufferUsageMask) != 0)
            {
                return false;
            }
            if (HasBufferUsage(desc.usage, BufferUsage::Constant)
                && desc.size > (std::numeric_limits<std::uint64_t>::max)() - 255)
            {
                return false;
            }
            if (desc.memory == MemoryType::Upload
                && HasBufferUsage(desc.usage, BufferUsage::CopyDestination))
            {
                return false;
            }
            if (desc.memory == MemoryType::Readback
                && usages != static_cast<std::uint32_t>(BufferUsage::CopyDestination))
            {
                return false;
            }
            return true;
        }

        bool IsTextureDescValid(const TextureDesc& desc)
        {
            const std::uint32_t usages = static_cast<std::uint32_t>(desc.usage);
            const bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
            const bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
            const bool storage = HasTextureUsage(desc.usage, TextureUsage::Storage);
            if (desc.extent.width == 0
                || desc.extent.height == 0
                || desc.depthOrLayers == 0
                || desc.depthOrLayers > (std::numeric_limits<std::uint16_t>::max)()
                || desc.mipLevels == 0
                || desc.mipLevels > (std::numeric_limits<std::uint16_t>::max)()
                || desc.sampleCount != 1
                || desc.format == TextureFormat::Unknown
                || usages == 0
                || (usages & ~TextureUsageMask) != 0
                || (renderTarget && depthStencil)
                || (depthStencil && desc.format != TextureFormat::D32Float)
                || (false == depthStencil && desc.format == TextureFormat::D32Float)
                || (storage && (depthStencil || IsSrgbFormat(desc.format))))
            {
                return false;
            }
            return true;
        }
    }

    BufferHandle D3D12Device::CreateBuffer(const BufferDesc& desc)
    {
        if (m_status != FrameStatus::Ready
            || m_device == nullptr
            || m_frameActive
            || false == IsBufferDescValid(desc))
        {
            return {};
        }

        CollectRetiredResources();

        std::uint32_t slotIndex = MaxBuffers;
        for (std::uint32_t index = 0; index < MaxBuffers; ++index)
        {
            if (false == m_buffers[index].occupied && m_buffers[index].resource == nullptr)
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == MaxBuffers)
        {
            return {};
        }

        std::uint64_t resourceSize = static_cast<std::uint64_t>(desc.size);
        if (HasBufferUsage(desc.usage, BufferUsage::Constant))
        {
            resourceSize = (resourceSize + 255) & ~std::uint64_t{255};
        }

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = resourceSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        const D3D12_HEAP_PROPERTIES heapProperties = BuildHeapProperties(desc.memory);
        const D3D12_RESOURCE_STATES initialState = InitialBufferState(desc.memory);
        ComPtr<ID3D12Resource> resource;
        if (FAILED(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&resource))))
        {
            return {};
        }

        D3D12BufferState& state = m_buffers[slotIndex];
        state.resource = resource;
        state.desc = desc;
        state.state = initialState;
        state.allocatedSize = resourceSize;
        state.mappedData = nullptr;
        if (desc.memory == MemoryType::Upload)
        {
            const D3D12_RANGE noReadRange = {0, 0};
            if (FAILED(resource->Map(0, &noReadRange, &state.mappedData)))
            {
                state.resource.Reset();
                state.desc = {};
                state.allocatedSize = 0;
                return {};
            }
        }
        state.retirementFence = 0;
        state.occupied = true;
        return {slotIndex, state.generation};
    }

    void D3D12Device::DestroyBuffer(BufferHandle buffer)
    {
        if (buffer.index >= MaxBuffers)
        {
            return;
        }

        D3D12BufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return;
        }

        state.occupied = false;
        state.generation = NextGeneration(state.generation);
        state.retirementFence = m_frameActive
            ? PendingRetirementFence
            : m_lastSubmittedFenceValue;
        if (m_frameActive)
        {
            m_hasPendingRetirementFence = true;
        }
    }

    bool D3D12Device::WriteBuffer(
        BufferHandle buffer,
        std::size_t offset,
        JArrayView<std::byte> data)
    {
        if (buffer.index >= MaxBuffers || data.size == 0 || data.data == nullptr)
        {
            return false;
        }

        D3D12BufferState& state = m_buffers[buffer.index];
        if (false == state.occupied
            || state.generation != buffer.generation
            || state.desc.memory != MemoryType::Upload
            || state.mappedData == nullptr
            || offset > state.allocatedSize
            || data.size > state.allocatedSize - offset)
        {
            return false;
        }

        std::memcpy(
            static_cast<std::byte*>(state.mappedData) + offset,
            data.data,
            data.size);
        return true;
    }

    TextureHandle D3D12Device::CreateTexture(const TextureDesc& desc)
    {
        if (m_status != FrameStatus::Ready
            || m_device == nullptr
            || m_frameActive
            || false == IsTextureDescValid(desc))
        {
            return {};
        }

        CollectRetiredResources();

        std::uint32_t slotIndex = MaxTextures;
        for (std::uint32_t index = 0; index < MaxTextures; ++index)
        {
            if (false == m_textures[index].occupied && m_textures[index].resource == nullptr)
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == MaxTextures)
        {
            return {};
        }

        const bool sampled = HasTextureUsage(desc.usage, TextureUsage::Sampled);
        const bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
        const bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
        const bool storage = HasTextureUsage(desc.usage, TextureUsage::Storage);

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Width = desc.extent.width;
        resourceDesc.Height = desc.extent.height;
        resourceDesc.DepthOrArraySize = static_cast<std::uint16_t>(desc.depthOrLayers);
        resourceDesc.MipLevels = static_cast<std::uint16_t>(desc.mipLevels);
        resourceDesc.Format = depthStencil && sampled
            ? DXGI_FORMAT_R32_TYPELESS
            : ToNativeFormat(desc.format);
        resourceDesc.SampleDesc.Count = desc.sampleCount;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (renderTarget)
        {
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (depthStencil)
        {
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (storage)
        {
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (false == sampled)
        {
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        ComPtr<ID3D12Resource> resource;
        if (FAILED(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&resource))))
        {
            return {};
        }

        D3D12TextureState& state = m_textures[slotIndex];
        state.resource = resource;
        state.desc = desc;
        state.state = D3D12_RESOURCE_STATE_COMMON;
        state.retirementFence = 0;
        state.renderTargetDescriptor = {};
        state.depthStencilDescriptor = {};

        if (renderTarget)
        {
            const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
                m_textureRenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
            state.renderTargetDescriptor.ptr = heapStart.ptr
                + static_cast<SIZE_T>(slotIndex) * m_textureRenderTargetDescriptorStride;
            m_device->CreateRenderTargetView(resource.Get(), nullptr, state.renderTargetDescriptor);
        }
        if (depthStencil)
        {
            const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
                m_textureDepthStencilHeap->GetCPUDescriptorHandleForHeapStart();
            state.depthStencilDescriptor.ptr = heapStart.ptr
                + static_cast<SIZE_T>(slotIndex) * m_textureDepthStencilDescriptorStride;
            D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
            viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
            viewDesc.ViewDimension = desc.depthOrLayers == 1
                ? D3D12_DSV_DIMENSION_TEXTURE2D
                : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            if (desc.depthOrLayers > 1)
            {
                viewDesc.Texture2DArray.ArraySize = desc.depthOrLayers;
            }
            m_device->CreateDepthStencilView(resource.Get(), &viewDesc, state.depthStencilDescriptor);
        }

        state.occupied = true;
        return {TextureResourceBase + slotIndex, state.generation};
    }

    void D3D12Device::DestroyTexture(TextureHandle texture)
    {
        if (texture.index < TextureResourceBase)
        {
            return;
        }

        const std::uint32_t slotIndex = texture.index - TextureResourceBase;
        if (slotIndex >= MaxTextures)
        {
            return;
        }

        D3D12TextureState& state = m_textures[slotIndex];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return;
        }

        state.occupied = false;
        state.generation = NextGeneration(state.generation);
        state.retirementFence = m_frameActive
            ? PendingRetirementFence
            : m_lastSubmittedFenceValue;
        if (m_frameActive)
        {
            m_hasPendingRetirementFence = true;
        }
    }

    bool D3D12Device::ResolveRenderTarget(TextureHandle texture, D3D12RenderTargetBinding& binding)
    {
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }

        if (texture.index < TextureResourceBase)
        {
            const std::uint32_t localIndex = texture.index - BackBufferTextureBase;
            const std::uint32_t swapchainIndex = localIndex / MaxBackBuffers;
            const std::uint32_t backBufferIndex = localIndex % MaxBackBuffers;
            if (swapchainIndex >= MaxSwapchains)
            {
                return false;
            }

            D3D12SwapchainState& swapchain = m_swapchains[swapchainIndex];
            if (false == swapchain.occupied || backBufferIndex >= swapchain.desc.bufferCount)
            {
                return false;
            }

            D3D12BackBuffer& backBuffer = swapchain.backBuffers[backBufferIndex];
            if (backBuffer.handle != texture)
            {
                return false;
            }

            binding.resource = backBuffer.resource.Get();
            binding.descriptor = backBuffer.descriptor;
            binding.format = ToNativeFormat(swapchain.desc.format);
            binding.state = &backBuffer.state;
            return true;
        }

        const std::uint32_t slotIndex = texture.index - TextureResourceBase;
        if (slotIndex >= MaxTextures)
        {
            return false;
        }

        D3D12TextureState& state = m_textures[slotIndex];
        if (false == state.occupied
            || state.generation != texture.generation
            || false == HasTextureUsage(state.desc.usage, TextureUsage::RenderTarget))
        {
            return false;
        }

        binding.resource = state.resource.Get();
        binding.descriptor = state.renderTargetDescriptor;
        binding.format = ToNativeFormat(state.desc.format);
        binding.state = &state.state;
        return true;
    }

    bool D3D12Device::ResolveBuffer(BufferHandle buffer, D3D12BufferBinding& binding)
    {
        if (buffer.index >= MaxBuffers)
        {
            return false;
        }

        D3D12BufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return false;
        }

        binding.resource = state.resource.Get();
        binding.gpuAddress = state.resource->GetGPUVirtualAddress();
        binding.size = state.allocatedSize;
        binding.usage = state.desc.usage;
        return true;
    }

    void D3D12Device::CollectRetiredResources()
    {
        if (m_fence == nullptr)
        {
            return;
        }

        const std::uint64_t completedFence = m_fence->GetCompletedValue();
        for (D3D12BufferState& state : m_buffers)
        {
            if (false == state.occupied
                && state.resource != nullptr
                && state.retirementFence != PendingRetirementFence
                && state.retirementFence <= completedFence)
            {
                if (state.mappedData != nullptr)
                {
                    state.resource->Unmap(0, nullptr);
                }
                state.resource.Reset();
                state.desc = {};
                state.state = D3D12_RESOURCE_STATE_COMMON;
                state.mappedData = nullptr;
                state.allocatedSize = 0;
                state.retirementFence = 0;
            }
        }

        for (D3D12TextureState& state : m_textures)
        {
            if (false == state.occupied
                && state.resource != nullptr
                && state.retirementFence != PendingRetirementFence
                && state.retirementFence <= completedFence)
            {
                state.resource.Reset();
                state.desc = {};
                state.renderTargetDescriptor = {};
                state.depthStencilDescriptor = {};
                state.state = D3D12_RESOURCE_STATE_COMMON;
                state.retirementFence = 0;
            }
        }

        for (D3D12PipelineState& state : m_graphicsPipelines)
        {
            if (false == state.occupied
                && state.pipeline != nullptr
                && state.retirementFence != PendingRetirementFence
                && state.retirementFence <= completedFence)
            {
                state.pipeline.Reset();
                state.rootSignature.Reset();
                state.topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
                state.pushConstantCount = 0;
                state.retirementFence = 0;
            }
        }
    }

    void D3D12Device::AssignPendingRetirementFences(std::uint64_t fenceValue)
    {
        if (false == m_hasPendingRetirementFence)
        {
            return;
        }

        for (D3D12BufferState& state : m_buffers)
        {
            if (false == state.occupied
                && state.resource != nullptr
                && state.retirementFence == PendingRetirementFence)
            {
                state.retirementFence = fenceValue;
            }
        }
        for (D3D12TextureState& state : m_textures)
        {
            if (false == state.occupied
                && state.resource != nullptr
                && state.retirementFence == PendingRetirementFence)
            {
                state.retirementFence = fenceValue;
            }
        }
        for (D3D12PipelineState& state : m_graphicsPipelines)
        {
            if (false == state.occupied
                && state.pipeline != nullptr
                && state.retirementFence == PendingRetirementFence)
            {
                state.retirementFence = fenceValue;
            }
        }
        m_hasPendingRetirementFence = false;
    }

    void D3D12Device::ReleaseAllResources()
    {
        for (D3D12BufferState& state : m_buffers)
        {
            if (state.resource != nullptr && state.mappedData != nullptr)
            {
                state.resource->Unmap(0, nullptr);
            }
            state.resource.Reset();
            state.desc = {};
            state.state = D3D12_RESOURCE_STATE_COMMON;
            state.mappedData = nullptr;
            state.allocatedSize = 0;
            state.retirementFence = 0;
            state.occupied = false;
        }
        for (D3D12TextureState& state : m_textures)
        {
            state.resource.Reset();
            state.desc = {};
            state.renderTargetDescriptor = {};
            state.depthStencilDescriptor = {};
            state.state = D3D12_RESOURCE_STATE_COMMON;
            state.retirementFence = 0;
            state.occupied = false;
        }
        for (D3D12PipelineState& state : m_graphicsPipelines)
        {
            state.pipeline.Reset();
            state.rootSignature.Reset();
            state.topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            state.pushConstantCount = 0;
            state.retirementFence = 0;
            state.occupied = false;
        }
    }

    bool D3D12Device::ResolveReadableTexture(
        TextureHandle texture,
        ID3D12Resource*& resource,
        D3D12_RESOURCE_STATES*& state,
        TextureDesc& desc)
    {
        resource = nullptr;
        state = nullptr;
        if (texture.index >= BackBufferTextureBase && texture.index < TextureResourceBase)
        {
            const std::uint32_t offset = texture.index - BackBufferTextureBase;
            const std::uint32_t swapchainIndex = offset / MaxBackBuffers;
            const std::uint32_t backBufferIndex = offset % MaxBackBuffers;
            if (swapchainIndex >= MaxSwapchains)
            {
                return false;
            }
            D3D12SwapchainState& swapchain = m_swapchains[swapchainIndex];
            if (false == swapchain.occupied || backBufferIndex >= swapchain.desc.bufferCount)
            {
                return false;
            }
            D3D12BackBuffer& backBuffer = swapchain.backBuffers[backBufferIndex];
            if (false == (backBuffer.handle == texture) || backBuffer.resource == nullptr)
            {
                return false;
            }
            resource = backBuffer.resource.Get();
            state = &backBuffer.state;
            desc = {};
            desc.extent = swapchain.desc.extent;
            desc.format = swapchain.desc.format;
            return true;
        }

        if (texture.index < TextureResourceBase)
        {
            return false;
        }
        const std::uint32_t slotIndex = texture.index - TextureResourceBase;
        if (slotIndex >= MaxTextures)
        {
            return false;
        }
        D3D12TextureState& textureState = m_textures[slotIndex];
        if (false == textureState.occupied
            || textureState.generation != texture.generation
            || textureState.resource == nullptr)
        {
            return false;
        }
        resource = textureState.resource.Get();
        state = &textureState.state;
        desc = textureState.desc;
        return true;
    }

    bool D3D12Device::ReadTexture(
        TextureHandle texture,
        std::byte* destination,
        std::size_t destinationSize,
        TextureReadback& result)
    {
        result = {};
        // 프레임이 열려 있는 동안은 안 된다. 기록 중인 커맨드 리스트를 가로채게 된다.
        if (m_status != FrameStatus::Ready
            || m_device == nullptr
            || m_frameActive
            || destination == nullptr
            || destinationSize == 0)
        {
            return false;
        }

        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES* resourceState = nullptr;
        TextureDesc desc;
        if (false == ResolveReadableTexture(texture, resource, resourceState, desc))
        {
            return false;
        }

        const std::uint32_t bytesPerPixel = ReadbackPixelSize(desc.format);
        if (bytesPerPixel == 0 || desc.extent.width == 0 || desc.extent.height == 0)
        {
            return false;
        }
        const std::size_t tightRowPitch =
            static_cast<std::size_t>(desc.extent.width) * bytesPerPixel;
        const std::size_t requiredBytes = tightRowPitch * desc.extent.height;
        if (destinationSize < requiredBytes)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rowCount = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        m_device->GetCopyableFootprints(
            &resourceDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeInBytes, &totalBytes);
        if (totalBytes == 0 || rowCount == 0)
        {
            return false;
        }

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC stagingDesc = {};
        stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        stagingDesc.Width = totalBytes;
        stagingDesc.Height = 1;
        stagingDesc.DepthOrArraySize = 1;
        stagingDesc.MipLevels = 1;
        stagingDesc.Format = DXGI_FORMAT_UNKNOWN;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> staging;
        if (FAILED(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&staging))))
        {
            return false;
        }

        // 진단 경로라 흐름을 통째로 비우고 쓴다. 프레임 안에서 부르는 것은 이미 막았다.
        WaitIdle();
        if (FAILED(m_commandAllocators[0]->Reset())
            || FAILED(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr)))
        {
            MarkDeviceLost();
            return false;
        }

        const D3D12_RESOURCE_STATES entryState = *resourceState;
        const bool needsTransition = entryState != D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (needsTransition)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = entryState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            m_commandList->ResourceBarrier(1, &barrier);
        }

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION target = {};
        target.pResource = staging.Get();
        target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        target.PlacedFootprint = footprint;
        m_commandList->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);

        if (needsTransition)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter = entryState;
            m_commandList->ResourceBarrier(1, &barrier);
        }

        if (FAILED(m_commandList->Close()))
        {
            MarkDeviceLost();
            return false;
        }
        ID3D12CommandList* commandLists[] = {m_commandList.Get()};
        m_graphicsQueue->ExecuteCommandLists(1, commandLists);
        ++m_lastSubmittedFenceValue;
        if (FAILED(m_graphicsQueue->Signal(m_fence.Get(), m_lastSubmittedFenceValue))
            || false == WaitForFence(m_lastSubmittedFenceValue))
        {
            MarkDeviceLost();
            return false;
        }

        void* mapped = nullptr;
        const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(staging->Map(0, &readRange, &mapped)) || mapped == nullptr)
        {
            return false;
        }
        const auto* sourceBytes = static_cast<const std::byte*>(mapped);
        const std::size_t copyPerRow =
            (std::min)(tightRowPitch, static_cast<std::size_t>(rowSizeInBytes));
        for (std::uint32_t row = 0; row < desc.extent.height; ++row)
        {
            std::memcpy(
                destination + static_cast<std::size_t>(row) * tightRowPitch,
                sourceBytes + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                copyPerRow);
        }
        const D3D12_RANGE noWriteRange = {0, 0};
        staging->Unmap(0, &noWriteRange);

        result.extent = desc.extent;
        result.format = desc.format;
        result.rowPitch = static_cast<std::uint32_t>(tightRowPitch);
        result.writtenBytes = static_cast<std::uint32_t>(requiredBytes);
        return true;
    }

}
