#include "VulkanDevice.h"

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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
        // DXGI 의 R11G11B10_FLOAT 와 비트 배치가 같다 - 이름이 낮은 비트부터가 아니라 높은 비트부터 적혔을 뿐이다.
        case TextureFormat::R11G11B10Float:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case TextureFormat::R16Float:
            return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::RG16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::RGBA32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::Unknown:
            return VK_FORMAT_UNDEFINED;
        }
        return VK_FORMAT_UNDEFINED;
    }

    UInt32 VulkanPixelSize(TextureFormat format)
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

    Bool VulkanDevice::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
        VkMemoryPropertyFlags fallback, VkDeviceMemory& memory, Bool& hostVisible)
    {
        // 자원마다 할당 하나다. 자원 수의 상한(버퍼 1024, 텍스처 512)이 드라이버의 할당 상한(4096) 아래다. `[가정]`
        for (Int32 pass = 0; pass < 2; ++pass)
        {
            const VkMemoryPropertyFlags flags = pass == 0 ? wanted : fallback;
            if (flags == 0)
            {
                continue;
            }
            for (UInt32 type = 0; type < m_memoryProperties.memoryTypeCount; ++type)
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

    Bool VulkanDevice::CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory)
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
        Bool hostVisible = false;
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

    Bool VulkanDevice::CreateStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped)
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
        Bool hostVisible = false;
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
        if (HasBufferUsage(desc.usage, BufferUsage::Storage))
        {
            // 구조화 버퍼는 GPU 가 채운다(`Device` 만). 원소 크기는 4 의 배수이고 크기를 나눠떨어지게 한다(D3D 와 같은 계약).
            if (desc.memory != MemoryType::Device || desc.stride == 0 || desc.stride % 4 != 0 || desc.size % desc.stride != 0)
            {
                return {};
            }
            info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
        Bool hostVisible = false;
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
        const UInt32 generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    Bool VulkanDevice::WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data)
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

    Bool VulkanDevice::ResolveBuffer(BufferHandle buffer, VkBuffer& native, BufferDesc& desc)
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
        const Bool depth = desc.format == TextureFormat::D32Float;
        const Bool renderTarget = HasTextureUsage(desc.usage, TextureUsage::RenderTarget);
        const Bool depthStencil = HasTextureUsage(desc.usage, TextureUsage::DepthStencil);
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive
            || desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.width > 16384
            || desc.extent.height > 16384 || false == IsTextureDimensionValid(desc, GetCaps())
            || (depthStencil && desc.mipLevels != 1)
            || desc.sampleCount != 1 || format == VK_FORMAT_UNDEFINED
            || (static_cast<JBro::UInt32>(desc.usage) & ~TextureUsageMask) != 0
            || desc.usage == TextureUsage::None
            || (HasTextureUsage(desc.usage, TextureUsage::Storage) && (depth || desc.format == TextureFormat::RGBA8UnormSrgb
                || desc.format == TextureFormat::BGRA8UnormSrgb))
            || (depth && (renderTarget || false == depthStencil)) || (false == depth && depthStencil))
        {
            return {};
        }
        // 포맷이 그 쓰임을 지원하는지 장치에 묻는다. R11G11B10 의 색 첨부처럼 명세가 보장하지 않는 것이 있다 - 묻지 않으면
        // 이미지는 만들어지고 그리는 자리에서 검증 오류가 난다. D3D 는 이 포맷들을 모두 필수로 지원한다.
        VkFormatFeatureFlags wanted = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if (HasTextureUsage(desc.usage, TextureUsage::Sampled))
        {
            wanted |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        }
        if (renderTarget)
        {
            wanted |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        }
        if (HasTextureUsage(desc.usage, TextureUsage::Storage))
        {
            // 읽고 쓰는 텍스처(컴퓨트의 u#). RGBA8·RGBA16F 등은 필수이고 BGRA8·R11G11B10 은 장치마다 다르다.
            wanted |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        }
        if (depthStencil)
        {
            wanted |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        VkFormatProperties support = {};
        vk.vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &support);
        if ((support.optimalTilingFeatures & wanted) != wanted)
        {
            return {};
        }
        UInt32 index = MaxTextures;
        for (UInt32 at = 0; at < MaxTextures; ++at)
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
        const Bool volume = desc.dimension == TextureDimension::Texture3D;
        info.imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {desc.extent.width, desc.extent.height, volume ? desc.depthOrLayers.Get() : 1u};
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
        if (HasTextureUsage(desc.usage, TextureUsage::Storage))
        {
            info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
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
        view.viewType = volume ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = desc.mipLevels;
        view.subresourceRange.layerCount = 1;
        Bool viewsMade = vk.vkCreateImageView(m_device, &view, nullptr, &state.view) == VK_SUCCESS;
        // mip 이 둘 이상이면 mip 하나씩의 뷰도 만든다. 그리기 경로에서 뷰를 만들지 않으려고 미리 다 만든다.
        for (UInt32 mip = 0; viewsMade && desc.mipLevels > 1 && mip < desc.mipLevels; ++mip)
        {
            view.subresourceRange.baseMipLevel = mip;
            view.subresourceRange.levelCount = 1;
            viewsMade = vk.vkCreateImageView(m_device, &view, nullptr, &state.mipViews[mip]) == VK_SUCCESS;
        }
        if (false == viewsMade)
        {
            for (VkImageView& mipView : state.mipViews)
            {
                if (mipView != VK_NULL_HANDLE)
                {
                    vk.vkDestroyImageView(m_device, mipView, nullptr);
                }
                mipView = VK_NULL_HANDLE;
            }
            if (state.view != VK_NULL_HANDLE)
            {
                vk.vkDestroyImageView(m_device, state.view, nullptr);
            }
            vk.vkDestroyImage(m_device, state.image, nullptr);
            vk.vkFreeMemory(m_device, state.memory, nullptr);
            state.image = VK_NULL_HANDLE;
            state.memory = VK_NULL_HANDLE;
            state.view = VK_NULL_HANDLE;
            return {};
        }
        for (VkImageLayout& layout : state.layouts)
        {
            layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
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
        for (VkImageView mipView : state.mipViews)
        {
            if (mipView != VK_NULL_HANDLE)
            {
                Retire(VulkanRetiredObject::Kind::ImageView, reinterpret_cast<std::uint64_t>(mipView));
            }
        }
        Retire(VulkanRetiredObject::Kind::Image, reinterpret_cast<std::uint64_t>(state.image));
        Retire(VulkanRetiredObject::Kind::Memory, reinterpret_cast<std::uint64_t>(state.memory));
        const UInt32 generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    Bool VulkanDevice::WriteTexture(TextureHandle texture, UInt32 mipLevel, JArrayView<std::byte> data)
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
        UInt32 width = state.desc.extent.width >> mipLevel;
        UInt32 height = state.desc.extent.height >> mipLevel;
        width = width == 0 ? UInt32(1) : width;
        height = height == 0 ? UInt32(1) : height;
        // 3D 는 조각을 차례로 이어 받는다(`IRHIDevice::WriteTexture` 의 계약).
        const UInt32 depth = state.desc.dimension == TextureDimension::Texture3D ? state.desc.depthOrLayers : UInt32(1);
        const std::size_t required = static_cast<std::size_t>(width) * height * depth * VulkanPixelSize(state.desc.format);
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
        Bool ok = BeginOneShot();
        if (ok)
        {
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT, state.layouts[mipLevel],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel, 1);
            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, depth};
            vk.vkCmdCopyBufferToImage(m_oneShotCommands, staging, state.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);
            // 올린 뒤에는 읽히는 것이 보통이다. Sampled 가 아니면 GENERAL 에 둔다 - 다음 패스가 알아서 바꾼다.
            const VkImageLayout after = HasTextureUsage(state.desc.usage, TextureUsage::Sampled)
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_GENERAL;
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, after, mipLevel, 1);
            state.layouts[mipLevel] = after;
            ok = EndOneShot();
        }
        vk.vkUnmapMemory(m_device, stagingMemory);
        vk.vkDestroyBuffer(m_device, staging, nullptr);
        vk.vkFreeMemory(m_device, stagingMemory, nullptr);
        return ok;
    }

    Bool VulkanDevice::WriteTextureRegion(TextureHandle texture, UInt32 mipLevel, UInt32 x, UInt32 y,
        UInt32 width, UInt32 height, JArrayView<std::byte> data, UInt32 rowPitch)
    {
        if (m_status != FrameStatus::Ready || m_frameActive || false == texture.IsValid()
            || texture.index < TextureResourceBase || texture.index - TextureResourceBase >= MaxTextures
            || data.data == nullptr || width == 0 || height == 0)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation || mipLevel >= state.desc.mipLevels
            || state.desc.format == TextureFormat::D32Float || state.desc.dimension != TextureDimension::Texture2D)
        {
            return false;
        }
        UInt32 levelWidth = state.desc.extent.width >> mipLevel;
        UInt32 levelHeight = state.desc.extent.height >> mipLevel;
        levelWidth = levelWidth == 0 ? UInt32(1) : levelWidth;
        levelHeight = levelHeight == 0 ? UInt32(1) : levelHeight;
        const std::size_t rowBytes = static_cast<std::size_t>(width) * VulkanPixelSize(state.desc.format);
        if (rowBytes == 0 || x + width > levelWidth || y + height > levelHeight || rowPitch < rowBytes
            || data.size < static_cast<std::size_t>(rowPitch) * (height - 1) + rowBytes)
        {
            return false;
        }
        // 행을 빽빽하게 모아 스테이징에 둔다(행 간격이 큰 페이지의 한 조각이어도).
        const std::size_t required = rowBytes * height;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        if (false == CreateStagingBuffer(required, staging, stagingMemory, mapped))
        {
            return false;
        }
        for (UInt32 row = 0; row < height; ++row)
        {
            std::memcpy(static_cast<std::byte*>(mapped) + static_cast<std::size_t>(row) * rowBytes,
                data.data + static_cast<std::size_t>(row) * rowPitch, rowBytes);
        }
        Bool ok = BeginOneShot();
        if (ok)
        {
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT, state.layouts[mipLevel],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel, 1);
            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<JBro::Int32>(x), static_cast<JBro::Int32>(y), 0};
            region.imageExtent = {width, height, 1};
            vk.vkCmdCopyBufferToImage(m_oneShotCommands, staging, state.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);
            const VkImageLayout after = HasTextureUsage(state.desc.usage, TextureUsage::Sampled)
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_GENERAL;
            TransitionImage(m_oneShotCommands, state.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, after, mipLevel, 1);
            state.layouts[mipLevel] = after;
            ok = EndOneShot();
        }
        vk.vkUnmapMemory(m_device, stagingMemory);
        vk.vkDestroyBuffer(m_device, staging, nullptr);
        vk.vkFreeMemory(m_device, stagingMemory, nullptr);
        return ok;
    }

    Bool VulkanDevice::ResolveAttachment(TextureHandle texture, UInt32 mipLevel, AttachmentView& view)
    {
        view = {};
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
            view.extent = {state.desc.extent.width, state.desc.extent.height};
            return true;
        }
        const UInt32 slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return false;
        }
        const Bool depth = state.desc.format == TextureFormat::D32Float;
        if ((depth && false == HasTextureUsage(state.desc.usage, TextureUsage::DepthStencil))
            || (false == depth && false == HasTextureUsage(state.desc.usage, TextureUsage::RenderTarget))
            || mipLevel >= state.desc.mipLevels)
        {
            return false;
        }
        view.image = state.image;
        view.view = state.desc.mipLevels > 1 ? state.mipViews[mipLevel] : state.view;
        view.layout = &state.layouts[mipLevel];
        view.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.extent = {GetMipExtent(state.desc.extent.width, mipLevel), GetMipExtent(state.desc.extent.height, mipLevel)};
        view.mip = mipLevel;
        view.mipLevels = state.desc.mipLevels;
        view.sampled = HasTextureUsage(state.desc.usage, TextureUsage::Sampled);
        return true;
    }

    Bool VulkanDevice::ResolveStorageTexture(TextureHandle texture, UInt32 mipLevel, AttachmentView& view)
    {
        view = {};
        if (false == texture.IsValid() || texture.index < TextureResourceBase
            || texture.index - TextureResourceBase >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation
            || false == HasTextureUsage(state.desc.usage, TextureUsage::Storage) || mipLevel >= state.desc.mipLevels)
        {
            return false;
        }
        view.image = state.image;
        view.view = state.desc.mipLevels > 1 ? state.mipViews[mipLevel] : state.view;
        view.layout = &state.layouts[mipLevel];
        view.mip = mipLevel;
        view.mipLevels = state.desc.mipLevels;
        view.sampled = HasTextureUsage(state.desc.usage, TextureUsage::Sampled);
        return true;
    }

    Bool VulkanDevice::ResolveStorageBuffer(BufferHandle buffer, VkBuffer& native, VkDeviceSize& size)
    {
        if (false == buffer.IsValid() || buffer.index >= MaxBuffers)
        {
            return false;
        }
        VulkanBufferState& state = m_buffers[buffer.index];
        if (false == state.occupied || state.generation != buffer.generation
            || false == HasBufferUsage(state.desc.usage, BufferUsage::Storage))
        {
            return false;
        }
        native = state.buffer;
        size = state.desc.size;
        return true;
    }

    Bool VulkanDevice::ResolveSampledTexture(TextureHandle texture, UInt32 mipLevel, VkImageView& view)
    {
        if (false == texture.IsValid() || texture.index < TextureResourceBase
            || texture.index - TextureResourceBase >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[texture.index - TextureResourceBase];
        if (false == state.occupied || state.generation != texture.generation
            || false == HasTextureUsage(state.desc.usage, TextureUsage::Sampled)
            || (mipLevel != AllMips && mipLevel >= state.desc.mipLevels))
        {
            return false;
        }
        // 읽을 mip 이 모두 셰이더 읽기 레이아웃이어야 한다. 아니면 아직 올린 적도 그린 적도 없거나, 이 패스가 그리는 mip 이다 -
        // 렌더 패스 안에서는 레이아웃을 바꿀 수 없으니 거절한다.
        const UInt32 first = mipLevel == AllMips ? UInt32(0) : mipLevel;
        const UInt32 last = mipLevel == AllMips ? UInt32(state.desc.mipLevels - 1) : mipLevel;
        for (UInt32 mip = first; mip <= last; ++mip)
        {
            if (state.layouts[mip] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                return false;
            }
        }
        view = mipLevel == AllMips || state.desc.mipLevels == 1 ? state.view : state.mipViews[mipLevel];
        return true;
    }

    // ── 샘플러 ──────────────────────────────────────────────────────────────

    SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive)
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
        VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = ToNativeFilter(desc.magFilter);
        info.minFilter = ToNativeFilter(desc.minFilter);
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = ToNativeAddress(desc.addressU);
        info.addressModeV = ToNativeAddress(desc.addressV);
        info.addressModeW = ToNativeAddress(desc.addressW);
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
        const UInt32 generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    Bool VulkanDevice::ResolveSampler(SamplerHandle sampler, VkSampler& native)
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
