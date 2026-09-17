#include "VulkanDevice.h"

#include <cstring>

namespace JBro::Internal
{
    VkFormat ToVulkanFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8UnormSrgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::BGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8UnormSrgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::RGBA16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::D32Float:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::Unknown:
            return VK_FORMAT_UNDEFINED;
        }
        return VK_FORMAT_UNDEFINED;
    }

    std::uint32_t VulkanPixelSize(TextureFormat format)
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

        VkSamplerAddressMode ToNativeAddress(AddressMode mode)
        {
            return mode == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }

        VkFilter ToNativeFilter(FilterMode mode)
        {
            return mode == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        }
    }

    // ── 메모리 ──────────────────────────────────────────────────────────────

    bool VulkanDevice::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
        VkMemoryPropertyFlags fallback, VkDeviceMemory& memory, bool& hostVisible)
    {
        // 자원마다 할당 하나다. 자원 수의 상한(버퍼 1024, 텍스처 512)이 드라이버의 할당 상한(4096) 아래다. `[가정]`
        for (int pass = 0; pass < 2; ++pass)
        {
            const VkMemoryPropertyFlags flags = pass == 0 ? wanted : fallback;
            if (flags == 0)
            {
                continue;
            }
            for (std::uint32_t type = 0; type < m_memoryProperties.memoryTypeCount; ++type)
            {
                if ((requirements.memoryTypeBits & (1u << type)) == 0
                    || (m_memoryProperties.memoryTypes[type].propertyFlags & flags) != flags)
                {
                    continue;
                }
                VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocate.allocationSize = requirements.size;
                allocate.memoryTypeIndex = type;
                if (vk.vkAllocateMemory(m_device, &allocate, nullptr, &memory) == VK_SUCCESS)
                {
                    hostVisible = (m_memoryProperties.memoryTypes[type].propertyFlags
                                      & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
                    return true;
                }
            }
        }
        memory = VK_NULL_HANDLE;
        return false;
    }

    bool VulkanDevice::CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory)
    {
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        if (vk.vkCreateImage(m_device, &info, nullptr, &image) != VK_SUCCESS)
        {
            image = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryRequirements requirements = {};
        vk.vkGetImageMemoryRequirements(m_device, image, &requirements);
        bool hostVisible = false;
        if (false == AllocateMemory(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, memory, hostVisible)
            || vk.vkBindImageMemory(m_device, image, memory, 0) != VK_SUCCESS)
        {
            vk.vkDestroyImage(m_device, image, nullptr);
            if (memory != VK_NULL_HANDLE)
            {
                vk.vkFreeMemory(m_device, memory, nullptr);
            }
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    bool VulkanDevice::CreateStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped)
    {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        mapped = nullptr;
        VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vk.vkCreateBuffer(m_device, &info, nullptr, &buffer) != VK_SUCCESS)
        {
            buffer = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryRequirements requirements = {};
        vk.vkGetBufferMemoryRequirements(m_device, buffer, &requirements);
        bool hostVisible = false;
        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (false == AllocateMemory(requirements, host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, host, memory, hostVisible)
            || vk.vkBindBufferMemory(m_device, buffer, memory, 0) != VK_SUCCESS
            || vk.vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
        {
            vk.vkDestroyBuffer(m_device, buffer, nullptr);
            if (memory != VK_NULL_HANDLE)
            {
                vk.vkFreeMemory(m_device, memory, nullptr);
            }
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            mapped = nullptr;
            return false;
        }
        return true;
    }

    // ── 버퍼 ────────────────────────────────────────────────────────────────

    BufferHandle VulkanDevice::CreateBuffer(const BufferDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive
            || desc.size == 0 || desc.usage == BufferUsage::None || desc.size > (std::size_t{1} << 30))
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
        VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = desc.size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (HasBufferUsage(desc.usage, BufferUsage::Vertex))
        {
            info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }
        if (HasBufferUsage(desc.usage, BufferUsage::Index))
        {
            info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }
        if (HasBufferUsage(desc.usage, BufferUsage::Constant))
        {
            info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        }
        VulkanBufferState& state = m_buffers[index];
        if (vk.vkCreateBuffer(m_device, &info, nullptr, &state.buffer) != VK_SUCCESS)
        {
            state.buffer = VK_NULL_HANDLE;
            return {};
        }
        VkMemoryRequirements requirements = {};
        vk.vkGetBufferMemoryRequirements(m_device, state.buffer, &requirements);
        // 모든 버퍼가 호스트에서 보인다. `WriteBuffer` 는 매핑된 메모리에 memcpy 다(D3D12 의 업로드 힙과 같다).
        // Device 메모리는 DEVICE_LOCAL 이면서 보이는 종류(BAR)를 먼저 찾고, 없으면 그냥 보이는 것으로 간다. `[가정]`
        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkMemoryPropertyFlags wanted = desc.memory == MemoryType::Device
            ? host | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            : host;
        bool hostVisible = false;
        if (false == AllocateMemory(requirements, wanted, host, state.memory, hostVisible)
            || vk.vkBindBufferMemory(m_device, state.buffer, state.memory, 0) != VK_SUCCESS
            || vk.vkMapMemory(m_device, state.memory, 0, VK_WHOLE_SIZE, 0, &state.mapped) != VK_SUCCESS)
        {
            vk.vkDestroyBuffer(m_device, state.buffer, nullptr);
            if (state.memory != VK_NULL_HANDLE)
            {
                vk.vkFreeMemory(m_device, state.memory, nullptr);
            }
            state.buffer = VK_NULL_HANDLE;
            state.memory = VK_NULL_HANDLE;
            state.mapped = nullptr;
            return {};
        }
        state.desc = desc;
        state.occupied = true;
        return BufferHandle{index, state.generation};
    }

    void VulkanDevice::DestroyBuffer(BufferHandle buffer)
    {
        if (false == buffer.IsValid() || buffer.index >= MaxBuffers)
        {
            return;
        }
        VulkanBufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return;
        }
        if (state.mapped != nullptr)
        {
            vk.vkUnmapMemory(m_device, state.memory);
        }
        Retire(VulkanRetiredObject::Kind::Buffer, reinterpret_cast<std::uint64_t>(state.buffer));
        Retire(VulkanRetiredObject::Kind::Memory, reinterpret_cast<std::uint64_t>(state.memory));
        const std::uint32_t generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    bool VulkanDevice::WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data)
    {
        if (false == buffer.IsValid() || buffer.index >= MaxBuffers || data.data == nullptr || data.size == 0)
        {
            return false;
        }
        VulkanBufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation || state.mapped == nullptr
            || offset > state.desc.size || state.desc.size - offset < data.size)
        {
            return false;
        }
        std::memcpy(static_cast<std::byte*>(state.mapped) + offset, data.data, data.size);
        return true;
    }

    bool VulkanDevice::ResolveBuffer(BufferHandle buffer, VkBuffer& native, BufferDesc& desc)
    {
        if (false == buffer.IsValid() || buffer.index >= MaxBuffers)
        {
            return false;
        }
        VulkanBufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation)
        {
            return false;
        }
        native = state.buffer;
        desc = state.desc;
        return true;
    }

    // ── 텍스처 ──────────────────────────────────────────────────────────────

    TextureHandle VulkanDevice::CreateTexture(const TextureDesc& desc)
    {
        const VkFormat format = ToVulkanFormat(desc.format);
        const bool depth = desc.format == TextureFormat::D32Float;
        const bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
        const bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive
            || desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.width > 16384
            || desc.extent.height > 16384 || desc.depthOrLayers != 1 || desc.mipLevels == 0
            || desc.sampleCount != 1 || format == VK_FORMAT_UNDEFINED
            || (static_cast<std::uint32_t>(desc.usage) & ~TextureUsageMask) != 0
            || desc.usage == TextureUsage::None || HasTextureUsage(desc.usage, TextureUsage::Storage)
            || (depth && (renderTarget || false == depthStencil)) || (false == depth && depthStencil))
        {
            return {};
        }
        std::uint32_t index = MaxTextures;
        for (std::uint32_t at = 0; at < MaxTextures; ++at)
        {
            if (false == m_textures[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxTextures)
        {
            return {};
        }
        VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {desc.extent.width, desc.extent.height, 1};
        info.mipLevels = desc.mipLevels;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        // 올리기(`WriteTexture`)와 되읽기(`ReadTexture`)가 언제든 올 수 있어 전송 양쪽을 늘 켠다.
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (HasTextureUsage(desc.usage, TextureUsage::Sampled))
        {
            info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if (renderTarget)
        {
            info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (depthStencil)
        {
            info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VulkanTextureState& state = m_textures[index];
        if (false == CreateImage(info, state.image, state.memory))
        {
            return {};
        }
        VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = state.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = desc.mipLevels;
        view.subresourceRange.layerCount = 1;
        if (vk.vkCreateImageView(m_device, &view, nullptr, &state.view) != VK_SUCCESS)
        {
            vk.vkDestroyImage(m_device, state.image, nullptr);
            vk.vkFreeMemory(m_device, state.memory, nullptr);
            state.image = VK_NULL_HANDLE;
            state.memory = VK_NULL_HANDLE;
            state.view = VK_NULL_HANDLE;
            return {};
        }
        state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        state.format = format;
        state.desc = desc;
        state.occupied = true;
        return TextureHandle{TextureResourceBase + index, state.generation};
    }

    void VulkanDevice::DestroyTexture(TextureHandle texture)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase
            || texture.index - TextureResourceBase >= MaxTextures)
        {
            return;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return;
        }
        Retire(VulkanRetiredObject::Kind::ImageView, reinterpret_cast<std::uint64_t>(state.view));
        Retire(VulkanRetiredObject::Kind::Image, reinterpret_cast<std::uint64_t>(state.image));
        Retire(VulkanRetiredObject::Kind::Memory, reinterpret_cast<std::uint64_t>(state.memory));
        const std::uint32_t generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    bool VulkanDevice::WriteTexture(TextureHandle texture, std::uint32_t mipLevel, JArrayView<std::byte> data)
    {
        if (m_status != FrameStatus::Ready || m_frameActive || false == texture.IsValid()
            || texture.index < TextureResourceBase || texture.index - TextureResourceBase >= MaxTextures
            || data.data == nullptr || data.size == 0)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation || mipLevel >= state.desc.mipLevels
            || state.desc.format == TextureFormat::D32Float)
        {
            return false;
        }
        std::uint32_t width = state.desc.extent.width >> mipLevel;
        std::uint32_t height = state.desc.extent.height >> mipLevel;
        width = width == 0 ? 1 : width;
        height = height == 0 ? 1 : height;
        const std::size_t required = static_cast<std::size_t>(width) * height * VulkanPixelSize(state.desc.format);
        if (data.size != required)
        {
            return false;
        }
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        if (false == CreateStagingBuffer(required, staging, stagingMemory, mapped))
        {
            return false;
        }
        std::memcpy(mapped, data.data, required);
        bool ok = BeginOneShot();
        if (ok)
        {
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT, state.layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
            vk.vkCmdCopyBufferToImage(m_oneShotCommands, staging, state.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);
            // 올린 뒤에는 읽히는 것이 보통이다. Sampled 가 아니면 GENERAL 에 둔다 - 다음 패스가 알아서 바꾼다.
            const VkImageLayout after = HasTextureUsage(state.desc.usage, TextureUsage::Sampled)
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_GENERAL;
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, after);
            state.layout = after;
            ok = EndOneShot();
        }
        vk.vkUnmapMemory(m_device, stagingMemory);
        vk.vkDestroyBuffer(m_device, staging, nullptr);
        vk.vkFreeMemory(m_device, stagingMemory, nullptr);
        return ok;
    }

    bool VulkanDevice::ResolveAttachment(TextureHandle texture, AttachmentView& view)
    {
        view = {};
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
            VulkanSwapchainState& state = m_swapchains[index];
            if (false == state.occupied || state.backBufferGeneration != texture.generation
                || false == m_frameActive || m_activeSwapchainIndex != index || state.currentImage >= state.imageCount)
            {
                return false;
            }
            view.image = state.images[state.currentImage];
            view.view = state.views[state.currentImage];
            view.layout = &state.layouts[state.currentImage];
            view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            return true;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return false;
        }
        const bool depth = state.desc.format == TextureFormat::D32Float;
        if ((depth && false == HasTextureUsage(state.desc.usage, TextureUsage::DepthStencil))
            || (false == depth && false == HasTextureUsage(state.desc.usage, TextureUsage::RenderTarget)))
        {
            return false;
        }
        view.image = state.image;
        view.view = state.view;
        view.layout = &state.layout;
        view.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.sampled = HasTextureUsage(state.desc.usage, TextureUsage::Sampled);
        return true;
    }

    bool VulkanDevice::ResolveAttachmentExtent(TextureHandle texture, VkExtent2D& extent)
    {
        extent = {};
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }
        if (texture.index < TextureResourceBase)
        {
            const std::uint32_t index = texture.index - BackBufferTextureBase;
            if (index >= MaxSwapchains || false == m_swapchains[index].occupied
                || m_swapchains[index].backBufferGeneration != texture.generation)
            {
                return false;
            }
            extent = {m_swapchains[index].desc.extent.width, m_swapchains[index].desc.extent.height};
            return true;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures || false == m_textures[slot].occupied
            || m_textures[slot].generation != texture.generation)
        {
            return false;
        }
        extent = {m_textures[slot].desc.extent.width, m_textures[slot].desc.extent.height};
        return true;
    }

    bool VulkanDevice::ResolveSampledTexture(TextureHandle texture, VkImageView& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase
            || texture.index - TextureResourceBase >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation
            || false == HasTextureUsage(state.desc.usage, TextureUsage::Sampled)
            || state.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            // 아직 셰이더가 읽을 레이아웃이 아니다(올린 적도 그린 적도 없다). 렌더 패스 안에서는 바꿀 수 없으니 거절한다.
            return false;
        }
        view = state.view;
        return true;
    }

    // ── 샘플러 ──────────────────────────────────────────────────────────────

    SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive)
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
        VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = ToNativeFilter(desc.magFilter);
        info.minFilter = ToNativeFilter(desc.minFilter);
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = ToNativeAddress(desc.addressU);
        info.addressModeV = ToNativeAddress(desc.addressV);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = VK_LOD_CLAMP_NONE;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        VulkanSamplerState& state = m_samplers[index];
        if (vk.vkCreateSampler(m_device, &info, nullptr, &state.sampler) != VK_SUCCESS)
        {
            state.sampler = VK_NULL_HANDLE;
            return {};
        }
        state.desc = desc;
        state.occupied = true;
        return SamplerHandle{index, state.generation};
    }

    void VulkanDevice::DestroySampler(SamplerHandle sampler)
    {
        if (false == sampler.IsValid() || sampler.index >= MaxSamplers)
        {
            return;
        }
        VulkanSamplerState& state = m_samplers[sampler.index];
        if (false == state.occupied || state.generation != sampler.generation)
        {
            return;
        }
        Retire(VulkanRetiredObject::Kind::Sampler, reinterpret_cast<std::uint64_t>(state.sampler));
        const std::uint32_t generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    bool VulkanDevice::ResolveSampler(SamplerHandle sampler, VkSampler& native)
    {
        if (false == sampler.IsValid() || sampler.index >= MaxSamplers)
        {
            return false;
        }
        VulkanSamplerState& state = m_samplers[sampler.index];
        if (false == state.occupied || state.generation != sampler.generation)
        {
            return false;
        }
        native = state.sampler;
        return true;
    }
}
