#include "VulkanDevice.h"

namespace JBro::Internal
{
    namespace
    {
        bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }

        VkAttachmentLoadOp ToNativeLoad(LoadOperation operation)
        {
            switch (operation)
            {
            case LoadOperation::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOperation::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOperation::Discard:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        }

        VkAttachmentStoreOp ToNativeStore(StoreOperation operation)
        {
            return operation == StoreOperation::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    void VulkanCommandContext::Bind(VulkanDevice* device)
    {
        m_device = device;
        Reset();
    }

    void VulkanCommandContext::BeginFrame(VkCommandBuffer commands, VkDescriptorPool descriptorPool)
    {
        Reset();
        m_commands = commands;
        m_descriptorPool = descriptorPool;
    }

    void VulkanCommandContext::Reset()
    {
        m_commands = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_activePipeline = nullptr;
        m_cachedSetCount = 0;
        m_cachedSetCursor = 0;
        m_sampledAtEndCount = 0;
        for (VkImageView& view : m_pendingTextures)
        {
            view = VK_NULL_HANDLE;
        }
        for (VkSampler& sampler : m_pendingSamplers)
        {
            sampler = VK_NULL_HANDLE;
        }
        m_descriptorsDirty = false;
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    bool VulkanCommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {
        if (m_device == nullptr || m_commands == VK_NULL_HANDLE || m_renderPassActive
            || desc.colorAttachments.data == nullptr || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments)
        {
            return false;
        }
        VulkanDevice::AttachmentView colors[MaxColorAttachments];
        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            if (false == m_device->ResolveAttachment(desc.colorAttachments.data[index].texture, colors[index])
                || colors[index].aspect != VK_IMAGE_ASPECT_COLOR_BIT)
            {
                return false;
            }
        }
        VulkanDevice::AttachmentView depth;
        if (desc.depthStencilAttachment != nullptr
            && (false == m_device->ResolveAttachment(desc.depthStencilAttachment->texture, depth)
                || depth.aspect != VK_IMAGE_ASPECT_DEPTH_BIT))
        {
            return false;
        }

        // 배리어는 패스 밖에서만 넣을 수 있다. 첨부를 전부 여기서 돌려 놓는다.
        VkRenderingAttachmentInfo colorInfos[MaxColorAttachments] = {};
        // 렌더 영역은 첨부 크기다. 첫 색 첨부의 크기를 쓴다 - 첨부들은 같은 크기여야 한다.
        const VkExtent2D extent = colors[0].extent;
        m_sampledAtEndCount = 0;
        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            const ColorAttachmentDesc& attachment = desc.colorAttachments.data[index];
            VulkanDevice::AttachmentView& view = colors[index];
            // 내용을 버리는 첨부는 UNDEFINED 에서 와도 된다 - 전이가 값을 지켜 줄 필요가 없다.
            const VkImageLayout from = attachment.loadOperation == LoadOperation::Load ? *view.layout
                                                                                         : VK_IMAGE_LAYOUT_UNDEFINED;
            VulkanDevice::TransitionImage(m_commands, view.image, VK_IMAGE_ASPECT_COLOR_BIT, from,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            *view.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkRenderingAttachmentInfo& info = colorInfos[index];
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageView = view.view;
            info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            info.loadOp = ToNativeLoad(attachment.loadOperation);
            info.storeOp = ToNativeStore(attachment.storeOperation);
            info.clearValue.color = {{attachment.clearColor.red, attachment.clearColor.green,
                attachment.clearColor.blue, attachment.clearColor.alpha}};
            if (view.sampled)
            {
                m_sampledAtEnd[m_sampledAtEndCount++] = attachment.texture;
            }
        }
        VkRenderingAttachmentInfo depthInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (depth.image != VK_NULL_HANDLE)
        {
            const VkImageLayout from = desc.depthStencilAttachment->depthLoadOperation == LoadOperation::Load
                ? *depth.layout
                : VK_IMAGE_LAYOUT_UNDEFINED;
            VulkanDevice::TransitionImage(m_commands, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, from,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            *depth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthInfo.imageView = depth.view;
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthInfo.loadOp = ToNativeLoad(desc.depthStencilAttachment->depthLoadOperation);
            depthInfo.storeOp = ToNativeStore(desc.depthStencilAttachment->depthStoreOperation);
            depthInfo.clearValue.depthStencil = {desc.depthStencilAttachment->clearDepth,
                desc.depthStencilAttachment->clearStencil};
        }
        VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = desc.colorAttachments.size;
        rendering.pColorAttachments = colorInfos;
        rendering.pDepthAttachment = depth.image != VK_NULL_HANDLE ? &depthInfo : nullptr;
        vk.vkCmdBeginRendering(m_commands, &rendering);
        // 뷰포트·시저는 동적이라 파이프라인마다 다시 걸 필요 없이 패스 처음에 첨부 전체로 둔다.
        SetViewport({0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
        SetScissor({0, 0, static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height)});
        m_renderPassActive = true;
        m_pipelineActive = false;
        m_activePipeline = nullptr;
        return true;
    }

    void VulkanCommandContext::EndRenderPass()
    {
        if (false == m_renderPassActive || m_commands == VK_NULL_HANDLE)
        {
            return;
        }
        vk.vkCmdEndRendering(m_commands);
        // Sampled 로 만든 색 첨부는 다음 패스가 읽을 수 있게 돌려 놓는다.
        for (std::uint32_t index = 0; index < m_sampledAtEndCount; ++index)
        {
            VulkanDevice::AttachmentView view;
            if (m_device->ResolveAttachment(m_sampledAtEnd[index], view))
            {
                VulkanDevice::TransitionImage(m_commands, view.image, VK_IMAGE_ASPECT_COLOR_BIT, *view.layout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                *view.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        m_sampledAtEndCount = 0;
        m_renderPassActive = false;
        m_pipelineActive = false;
        m_activePipeline = nullptr;
    }

    void VulkanCommandContext::SetViewport(const Viewport& viewport)
    {
        if (m_commands == VK_NULL_HANDLE || false == m_renderPassActive)
        {
            return;
        }
        // 높이를 음수로 주어 y 를 뒤집는다(Vulkan 1.1 부터 허용). 그러면 NDC 의 +y 가 위로 가서 D3D 와 같은
        // 투영 행렬을 그대로 쓴다.
        VkViewport native = {};
        native.x = viewport.x;
        native.y = viewport.y + viewport.height;
        native.width = viewport.width;
        native.height = -viewport.height;
        native.minDepth = viewport.minDepth;
        native.maxDepth = viewport.maxDepth;
        vk.vkCmdSetViewport(m_commands, 0, 1, &native);
    }

    void VulkanCommandContext::SetScissor(const ScissorRect& scissor)
    {
        if (m_commands == VK_NULL_HANDLE || false == m_renderPassActive)
        {
            return;
        }
        VkRect2D native = {};
        const std::int32_t left = scissor.left < 0 ? 0 : scissor.left;
        const std::int32_t top = scissor.top < 0 ? 0 : scissor.top;
        native.offset = {left, top};
        native.extent.width = scissor.right > left ? static_cast<std::uint32_t>(scissor.right - left) : 0;
        native.extent.height = scissor.bottom > top ? static_cast<std::uint32_t>(scissor.bottom - top) : 0;
        vk.vkCmdSetScissor(m_commands, 0, 1, &native);
    }

    bool VulkanCommandContext::SetGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (false == m_renderPassActive || m_device == nullptr)
        {
            return false;
        }
        VulkanPipelineState* state = m_device->ResolvePipeline(pipeline);
        if (state == nullptr)
        {
            return false;
        }
        vk.vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state->pipeline);
        m_activePipeline = state;
        m_pipelineActive = true;
        // 파이프라인을 걸면 묶어 둔 것은 버려진다 - D3D12 와 같은 계약이다. 새 레이아웃에 다시 묶는다.
        for (VkImageView& view : m_pendingTextures)
        {
            view = VK_NULL_HANDLE;
        }
        for (VkSampler& sampler : m_pendingSamplers)
        {
            sampler = VK_NULL_HANDLE;
        }
        m_descriptorsDirty = true;
        return true;
    }

    bool VulkanCommandContext::SetVertexBuffer(
        std::uint32_t slot,
        BufferHandle buffer,
        std::uint32_t stride,
        std::size_t offset)
    {
        VkBuffer native = VK_NULL_HANDLE;
        BufferDesc desc;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= MaxVertexSlots || stride == 0
            || false == m_device->ResolveBuffer(buffer, native, desc)
            || false == HasBufferUsage(desc.usage, BufferUsage::Vertex)
            || offset >= desc.size)
        {
            return false;
        }
        // 스트라이드는 파이프라인의 정점 입력이 이미 안다. 여기서는 위치만 건다.
        const VkDeviceSize nativeOffset = offset;
        vk.vkCmdBindVertexBuffers(m_commands, slot, 1, &native, &nativeOffset);
        return true;
    }

    bool VulkanCommandContext::SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset)
    {
        VkBuffer native = VK_NULL_HANDLE;
        BufferDesc desc;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || false == m_device->ResolveBuffer(buffer, native, desc)
            || false == HasBufferUsage(desc.usage, BufferUsage::Index)
            || offset >= desc.size)
        {
            return false;
        }
        vk.vkCmdBindIndexBuffer(m_commands, native, offset,
            format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        return true;
    }

    bool VulkanCommandContext::SetGraphicsConstants(JArrayView<std::byte> data)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || m_activePipeline == nullptr
            || data.size != m_activePipeline->pushConstantBytes || (data.size != 0 && data.data == nullptr))
        {
            return false;
        }
        if (data.size == 0)
        {
            return true;
        }
        vk.vkCmdPushConstants(m_commands, m_activePipeline->layout, m_activePipeline->pushConstantStages, 0,
            static_cast<std::uint32_t>(data.size), data.data);
        return true;
    }

    bool VulkanCommandContext::SetTexture(std::uint32_t slot, TextureHandle texture)
    {
        VkImageView view = VK_NULL_HANDLE;
        if (false == m_renderPassActive || false == m_pipelineActive || m_activePipeline == nullptr
            || slot >= m_activePipeline->sampledTextureCount || false == m_device->ResolveSampledTexture(texture, view))
        {
            return false;
        }
        if (m_pendingTextures[slot] != view)
        {
            m_pendingTextures[slot] = view;
            m_descriptorsDirty = true;
        }
        return true;
    }

    bool VulkanCommandContext::SetSampler(std::uint32_t slot, SamplerHandle sampler)
    {
        VkSampler native = VK_NULL_HANDLE;
        if (false == m_renderPassActive || false == m_pipelineActive || m_activePipeline == nullptr
            || slot >= m_activePipeline->samplerCount || false == m_device->ResolveSampler(sampler, native))
        {
            return false;
        }
        if (m_pendingSamplers[slot] != native)
        {
            m_pendingSamplers[slot] = native;
            m_descriptorsDirty = true;
        }
        return true;
    }

    bool VulkanCommandContext::BindPendingDescriptors()
    {
        if (m_activePipeline->setLayout == VK_NULL_HANDLE)
        {
            return true;
        }
        if (false == m_descriptorsDirty)
        {
            return true;
        }
        // 파이프라인이 선언한 자리가 다 채워져야 한다. 빈 디스크립터는 검증 오류다.
        for (std::uint32_t slot = 0; slot < m_activePipeline->sampledTextureCount; ++slot)
        {
            if (m_pendingTextures[slot] == VK_NULL_HANDLE)
            {
                return false;
            }
        }
        for (std::uint32_t slot = 0; slot < m_activePipeline->samplerCount; ++slot)
        {
            if (m_pendingSamplers[slot] == VK_NULL_HANDLE)
            {
                return false;
            }
        }
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (FindCachedSet(set))
        {
            vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_activePipeline->layout, 0, 1,
                &set, 0, nullptr);
            m_descriptorsDirty = false;
            return true;
        }
        VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = m_descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &m_activePipeline->setLayout;
        if (vk.vkAllocateDescriptorSets(m_device->GetNativeDevice(), &allocate, &set) != VK_SUCCESS)
        {
            return false;
        }
        RememberSet(set);
        VkDescriptorImageInfo images[MaxBoundTextures + MaxBoundSamplers] = {};
        VkWriteDescriptorSet writes[MaxBoundTextures + MaxBoundSamplers] = {};
        std::uint32_t writeCount = 0;
        for (std::uint32_t slot = 0; slot < m_activePipeline->sampledTextureCount; ++slot)
        {
            images[writeCount].imageView = m_pendingTextures[slot];
            images[writeCount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet& write = writes[writeCount];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = TextureBindingBase + slot;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.pImageInfo = &images[writeCount];
            ++writeCount;
        }
        for (std::uint32_t slot = 0; slot < m_activePipeline->samplerCount; ++slot)
        {
            images[writeCount].sampler = m_pendingSamplers[slot];
            VkWriteDescriptorSet& write = writes[writeCount];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = SamplerBindingBase + slot;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.pImageInfo = &images[writeCount];
            ++writeCount;
        }
        vk.vkUpdateDescriptorSets(m_device->GetNativeDevice(), writeCount, writes, 0, nullptr);
        vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_activePipeline->layout, 0, 1, &set,
            0, nullptr);
        m_descriptorsDirty = false;
        return true;
    }

    bool VulkanCommandContext::FindCachedSet(VkDescriptorSet& set) const
    {
        for (std::uint32_t index = 0; index < m_cachedSetCount; ++index)
        {
            const CachedSet& candidate = m_cachedSets[index];
            if (candidate.layout != m_activePipeline->setLayout)
            {
                continue;
            }
            bool same = true;
            for (std::uint32_t slot = 0; slot < m_activePipeline->sampledTextureCount && same; ++slot)
            {
                same = candidate.views[slot] == m_pendingTextures[slot];
            }
            for (std::uint32_t slot = 0; slot < m_activePipeline->samplerCount && same; ++slot)
            {
                same = candidate.samplers[slot] == m_pendingSamplers[slot];
            }
            if (same)
            {
                set = candidate.set;
                return true;
            }
        }
        return false;
    }

    void VulkanCommandContext::RememberSet(VkDescriptorSet set)
    {
        // 꽉 차면 가장 오래된 자리부터 돌려 쓴다. set 자체는 풀이 프레임 끝에 통째로 비운다.
        CachedSet& entry = m_cachedSets[m_cachedSetCursor];
        m_cachedSetCursor = (m_cachedSetCursor + 1) % CachedSets;
        m_cachedSetCount = m_cachedSetCount < CachedSets ? m_cachedSetCount + 1 : CachedSets;
        entry.layout = m_activePipeline->setLayout;
        entry.set = set;
        for (std::uint32_t slot = 0; slot < MaxBoundTextures; ++slot)
        {
            entry.views[slot] = slot < m_activePipeline->sampledTextureCount ? m_pendingTextures[slot] : VK_NULL_HANDLE;
        }
        for (std::uint32_t slot = 0; slot < MaxBoundSamplers; ++slot)
        {
            entry.samplers[slot] = slot < m_activePipeline->samplerCount ? m_pendingSamplers[slot] : VK_NULL_HANDLE;
        }
    }

    bool VulkanCommandContext::DrawIndexedInstanced(
        std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t baseVertex,
        std::uint32_t firstInstance)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || m_activePipeline == nullptr
            || indexCount == 0 || instanceCount == 0 || false == BindPendingDescriptors())
        {
            return false;
        }
        vk.vkCmdDrawIndexed(m_commands, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        return true;
    }
}
