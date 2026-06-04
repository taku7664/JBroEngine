#include "pch.h"
#include "VulkanCommandContext.h"

#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanSampler.h"
#include "Core/RHI/Vulkan/VulkanSwapchain.h"
#include "Core/RHI/Vulkan/VulkanTexture.h"

#if JBRO_RHI_VULKAN
#include <cstring>

namespace
{
constexpr std::uint32_t VulkanDescriptorSetsPerPool = 1024;
}
#endif

CVulkanCommandContext::~CVulkanCommandContext()
{
#if JBRO_RHI_VULKAN
	DestroyFrameSync();
#endif
}

#if JBRO_RHI_VULKAN
void CVulkanCommandContext::BindNativeContext(VkDevice device, VkQueue graphicsQueue, VkCommandPool commandPool, CVulkanSwapchain* swapchain)
{
	m_device = device;
	m_graphicsQueue = graphicsQueue;
	m_commandPool = commandPool;
	m_swapchain = swapchain;

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer);

	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore);
	vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore);

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence);

	VkDescriptorPool descriptorPool = CreateDescriptorPool(VulkanDescriptorSetsPerPool);
	if (descriptorPool != VK_NULL_HANDLE)
	{
		m_descriptorPools.push_back(descriptorPool);
	}
}
#endif

void CVulkanCommandContext::BeginFrame()
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || m_commandBuffer == VK_NULL_HANDLE || nullptr == m_swapchain)
	{
		return;
	}

	vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
	vkResetFences(m_device, 1, &m_inFlightFence);
	vkResetCommandBuffer(m_commandBuffer, 0);
	for (VkDescriptorPool descriptorPool : m_descriptorPools)
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			vkResetDescriptorPool(m_device, descriptorPool, 0);
		}
	}
	m_activeDescriptorPoolIndex = 0;
	m_currentPipeline = nullptr;
	m_boundConstantBuffer = nullptr;
	m_boundTexture = nullptr;
	m_boundSampler = nullptr;
	m_activeRenderTarget = nullptr;
	m_activeRenderImage = VK_NULL_HANDLE;
	m_activeRenderInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_activeRenderExtent = {};
	m_swapchain->AcquireNextImage(m_imageAvailableSemaphore);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
#endif
}

void CVulkanCommandContext::BeginRenderPass(const RenderPassDesc& desc)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer == VK_NULL_HANDLE || nullptr == m_swapchain || m_swapchain->GetRenderPass() == VK_NULL_HANDLE)
	{
		return;
	}

	VkFramebuffer framebuffer = m_swapchain->GetCurrentFramebuffer();
	VkExtent2D extent = m_swapchain->GetExtent();
	m_activeRenderTarget = nullptr;
	m_activeRenderImage = m_swapchain->GetCurrentImage();
	m_activeRenderInitialLayout = m_swapchain->GetCurrentImageLayout();

	if (desc.ColorAttachment.Target.IsValid())
	{
		CVulkanTexture* targetTexture = static_cast<CVulkanTexture*>(desc.ColorAttachment.Target.TryGet());
		if (nullptr == targetTexture || targetTexture->GetNativeImage() == VK_NULL_HANDLE)
		{
			return;
		}

		framebuffer = targetTexture->GetOrCreateFramebuffer(m_swapchain->GetRenderPass());
		if (framebuffer == VK_NULL_HANDLE)
		{
			return;
		}

		m_activeRenderTarget = targetTexture;
		m_activeRenderImage = targetTexture->GetNativeImage();
		m_activeRenderInitialLayout = targetTexture->GetCurrentLayout();
		extent.width = targetTexture->GetDesc().Width;
		extent.height = targetTexture->GetDesc().Height;
	}

	if (framebuffer == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0)
	{
		return;
	}
	m_activeRenderExtent = extent;
	if (m_activeRenderTarget)
	{
		TransitionTextureLayout(m_activeRenderTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	else
	{
		TransitionImageLayout(m_activeRenderImage, m_activeRenderInitialLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	VkClearValue clearValue = {};
	clearValue.color.float32[0] = desc.ColorAttachment.ClearColor.R;
	clearValue.color.float32[1] = desc.ColorAttachment.ClearColor.G;
	clearValue.color.float32[2] = desc.ColorAttachment.ClearColor.B;
	clearValue.color.float32[3] = desc.ColorAttachment.ClearColor.A;

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = m_swapchain->GetRenderPass();
	passInfo.framebuffer = framebuffer;
	passInfo.renderArea.offset = { 0, 0 };
	passInfo.renderArea.extent = extent;
	passInfo.clearValueCount = 1;
	passInfo.pClearValues = &clearValue;
	vkCmdBeginRenderPass(m_commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
	m_isRenderPassOpen = true;

	if (ERHILoadOp::Clear == desc.ColorAttachment.LoadOp)
	{
		VkClearAttachment attachment = {};
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = 0;
		attachment.clearValue = clearValue;

		VkClearRect clearRect = {};
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = extent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;
		vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &clearRect);
	}

	SetViewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
#else
	(void)desc;
#endif
}

void CVulkanCommandContext::EndRenderPass()
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer != VK_NULL_HANDLE && m_isRenderPassOpen)
	{
		vkCmdEndRenderPass(m_commandBuffer);
		m_isRenderPassOpen = false;
		if (m_activeRenderImage != VK_NULL_HANDLE)
		{
			TransitionImageLayout(
				m_activeRenderImage,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				m_activeRenderTarget ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			if (m_activeRenderTarget)
			{
				m_activeRenderTarget->SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			else if (m_swapchain)
			{
				m_swapchain->SetCurrentImageLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			}
		}
		m_activeRenderTarget = nullptr;
		m_activeRenderImage = VK_NULL_HANDLE;
		m_activeRenderInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		m_activeRenderExtent = {};
	}
#endif
}

void CVulkanCommandContext::EndFrame()
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || m_graphicsQueue == VK_NULL_HANDLE || m_commandBuffer == VK_NULL_HANDLE)
	{
		return;
	}

	if (m_isRenderPassOpen)
	{
		EndRenderPass();
	}
	vkEndCommandBuffer(m_commandBuffer);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;
	vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence);
	if (m_swapchain)
	{
		m_swapchain->SetPresentWaitSemaphore(m_renderFinishedSemaphore);
	}
#endif
}

void CVulkanCommandContext::SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer == VK_NULL_HANDLE || false == pipeline.IsValid())
	{
		return;
	}

	CVulkanGraphicsPipeline* vkPipeline = static_cast<CVulkanGraphicsPipeline*>(pipeline.TryGet());
	if (vkPipeline && vkPipeline->GetPipeline() != VK_NULL_HANDLE)
	{
		m_currentPipeline = vkPipeline;
		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetPipeline());
	}
#else
	(void)pipeline;
#endif
}

void CVulkanCommandContext::SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t, std::uint32_t offset)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer == VK_NULL_HANDLE || false == buffer.IsValid())
	{
		return;
	}
	CVulkanBuffer* vkBuffer = static_cast<CVulkanBuffer*>(buffer.TryGet());
	if (vkBuffer && vkBuffer->GetNativeBuffer() != VK_NULL_HANDLE)
	{
		VkBuffer nativeBuffer = vkBuffer->GetNativeBuffer();
		VkDeviceSize nativeOffset = offset;
		vkCmdBindVertexBuffers(m_commandBuffer, slot, 1, &nativeBuffer, &nativeOffset);
	}
#else
	(void)slot;
	(void)buffer;
	(void)offset;
#endif
}

void CVulkanCommandContext::SetIndexBuffer(SafePtr<IRHIBuffer> buffer)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer == VK_NULL_HANDLE || false == buffer.IsValid())
	{
		return;
	}
	CVulkanBuffer* vkBuffer = static_cast<CVulkanBuffer*>(buffer.TryGet());
	if (vkBuffer && vkBuffer->GetNativeBuffer() != VK_NULL_HANDLE)
	{
		vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer->GetNativeBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}
#else
	(void)buffer;
#endif
}

void CVulkanCommandContext::SetConstantBuffer(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHIBuffer> buffer)
{
#if JBRO_RHI_VULKAN
	if (slot == 0 && buffer.IsValid())
	{
		m_boundConstantBuffer = buffer;
	}
#else
	(void)buffer;
#endif
	(void)stage;
	(void)slot;
}

void CVulkanCommandContext::UpdateBuffer(SafePtr<IRHIBuffer> buffer, const void* data, std::size_t size)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || false == buffer.IsValid() || nullptr == data || size == 0)
	{
		return;
	}

	CVulkanBuffer* vkBuffer = static_cast<CVulkanBuffer*>(buffer.TryGet());
	if (nullptr == vkBuffer || vkBuffer->GetNativeMemory() == VK_NULL_HANDLE)
	{
		return;
	}

	void* mapped = nullptr;
	if (vkMapMemory(m_device, vkBuffer->GetNativeMemory(), 0, size, 0, &mapped) == VK_SUCCESS)
	{
		std::memcpy(mapped, data, size);
		vkUnmapMemory(m_device, vkBuffer->GetNativeMemory());
	}
#else
	(void)buffer;
	(void)data;
	(void)size;
#endif
}

void CVulkanCommandContext::SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture)
{
#if JBRO_RHI_VULKAN
	if (ERHIProgramStage::Pixel == stage && slot == 0 && texture.IsValid())
	{
		m_boundTexture = texture;
	}
#else
	(void)texture;
#endif
	(void)stage;
	(void)slot;
}

void CVulkanCommandContext::SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler)
{
#if JBRO_RHI_VULKAN
	if (ERHIProgramStage::Pixel == stage && slot == 0 && sampler.IsValid())
	{
		m_boundSampler = sampler;
	}
#else
	(void)sampler;
#endif
	(void)stage;
	(void)slot;
}

void CVulkanCommandContext::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer == VK_NULL_HANDLE)
	{
		return;
	}
	VkViewport viewport = {};
	viewport.x = x;
	viewport.y = y + height;
	viewport.width = width > 0.0f ? width : 1.0f;
	viewport.height = height > 0.0f ? -height : -1.0f;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = { static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) };
	scissor.extent = { static_cast<std::uint32_t>(width > 0.0f ? width : 1.0f), static_cast<std::uint32_t>(height > 0.0f ? height : 1.0f) };
	vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
#else
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)minDepth;
	(void)maxDepth;
#endif
}

void CVulkanCommandContext::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer != VK_NULL_HANDLE)
	{
		BindPendingDescriptors();
		vkCmdDraw(m_commandBuffer, vertexCount, 1, firstVertex, 0);
	}
#else
	(void)vertexCount;
	(void)firstVertex;
#endif
}

void CVulkanCommandContext::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex)
{
#if JBRO_RHI_VULKAN
	if (m_commandBuffer != VK_NULL_HANDLE)
	{
		BindPendingDescriptors();
		vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, firstIndex, baseVertex, 0);
	}
#else
	(void)indexCount;
	(void)firstIndex;
	(void)baseVertex;
#endif
}

#if JBRO_RHI_VULKAN
void CVulkanCommandContext::DestroyFrameSync()
{
	if (m_device == VK_NULL_HANDLE)
	{
		return;
	}

	vkDeviceWaitIdle(m_device);
	for (VkDescriptorPool descriptorPool : m_descriptorPools)
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_device, descriptorPool, nullptr);
		}
	}
	m_descriptorPools.clear();
	m_activeDescriptorPoolIndex = 0;
	if (m_inFlightFence != VK_NULL_HANDLE)
	{
		vkDestroyFence(m_device, m_inFlightFence, nullptr);
		m_inFlightFence = VK_NULL_HANDLE;
	}
	if (m_renderFinishedSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
		m_renderFinishedSemaphore = VK_NULL_HANDLE;
	}
	if (m_imageAvailableSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
		m_imageAvailableSemaphore = VK_NULL_HANDLE;
	}
	if (m_commandPool != VK_NULL_HANDLE && m_commandBuffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer);
		m_commandBuffer = VK_NULL_HANDLE;
	}
}

VkDescriptorPool CVulkanCommandContext::CreateDescriptorPool(std::uint32_t maxSets)
{
	if (m_device == VK_NULL_HANDLE || 0 == maxSets)
	{
		return VK_NULL_HANDLE;
	}

	VkDescriptorPoolSize poolSizes[3] = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = maxSets;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	poolSizes[1].descriptorCount = maxSets;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	poolSizes[2].descriptorCount = maxSets;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;

	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
	{
		return VK_NULL_HANDLE;
	}
	return descriptorPool;
}

bool CVulkanCommandContext::AllocateDescriptorSet(VkDescriptorSetLayout setLayout, VkDescriptorSet* outDescriptorSet)
{
	if (m_device == VK_NULL_HANDLE || setLayout == VK_NULL_HANDLE || nullptr == outDescriptorSet)
	{
		return false;
	}

	*outDescriptorSet = VK_NULL_HANDLE;
	if (m_descriptorPools.empty())
	{
		VkDescriptorPool descriptorPool = CreateDescriptorPool(VulkanDescriptorSetsPerPool);
		if (descriptorPool == VK_NULL_HANDLE)
		{
			return false;
		}
		m_descriptorPools.push_back(descriptorPool);
		m_activeDescriptorPoolIndex = 0;
	}

	for (std::size_t poolIndex = m_activeDescriptorPoolIndex; poolIndex < m_descriptorPools.size(); ++poolIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPools[poolIndex];
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &setLayout;

		if (vkAllocateDescriptorSets(m_device, &allocInfo, outDescriptorSet) == VK_SUCCESS)
		{
			m_activeDescriptorPoolIndex = poolIndex;
			return true;
		}
	}

	VkDescriptorPool descriptorPool = CreateDescriptorPool(VulkanDescriptorSetsPerPool);
	if (descriptorPool == VK_NULL_HANDLE)
	{
		return false;
	}
	m_descriptorPools.push_back(descriptorPool);
	m_activeDescriptorPoolIndex = m_descriptorPools.size() - 1;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;
	return vkAllocateDescriptorSets(m_device, &allocInfo, outDescriptorSet) == VK_SUCCESS;
}

void CVulkanCommandContext::BindPendingDescriptors()
{
	if (m_device == VK_NULL_HANDLE || m_commandBuffer == VK_NULL_HANDLE || nullptr == m_currentPipeline)
	{
		return;
	}
	if (m_currentPipeline->GetDescriptorSetLayout() == VK_NULL_HANDLE || m_currentPipeline->GetPipelineLayout() == VK_NULL_HANDLE)
	{
		return;
	}
	if (false == m_boundConstantBuffer.IsValid() || false == m_boundTexture.IsValid() || false == m_boundSampler.IsValid())
	{
		return;
	}

	CVulkanBuffer* constantBuffer = static_cast<CVulkanBuffer*>(m_boundConstantBuffer.TryGet());
	CVulkanTexture* texture = static_cast<CVulkanTexture*>(m_boundTexture.TryGet());
	CVulkanSampler* sampler = static_cast<CVulkanSampler*>(m_boundSampler.TryGet());
	if (nullptr == constantBuffer || nullptr == texture || nullptr == sampler
		|| constantBuffer->GetNativeBuffer() == VK_NULL_HANDLE
		|| texture->GetImageView() == VK_NULL_HANDLE
		|| sampler->GetNativeSampler() == VK_NULL_HANDLE)
	{
		return;
	}

	VkDescriptorSetLayout setLayout = m_currentPipeline->GetDescriptorSetLayout();
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	if (false == AllocateDescriptorSet(setLayout, &descriptorSet))
	{
		return;
	}

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = constantBuffer->GetNativeBuffer();
	bufferInfo.offset = 0;
	bufferInfo.range = constantBuffer->GetDesc().SizeInBytes;

	VkDescriptorImageInfo textureInfo = {};
	textureInfo.imageView = texture->GetImageView();
	textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo samplerInfo = {};
	samplerInfo.sampler = sampler->GetNativeSampler();

	VkWriteDescriptorSet writes[3] = {};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = descriptorSet;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &bufferInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = descriptorSet;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[1].pImageInfo = &textureInfo;

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = descriptorSet;
	writes[2].dstBinding = 2;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	writes[2].pImageInfo = &samplerInfo;

	vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
	vkCmdBindDescriptorSets(
		m_commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_currentPipeline->GetPipelineLayout(),
		0,
		1,
		&descriptorSet,
		0,
		nullptr);
}

void CVulkanCommandContext::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	if (m_commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout)
	{
		return;
	}

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		barrier.srcAccessMask = 0;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
	{
		barrier.srcAccessMask = 0;
		srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}

	if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
	{
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = 0;
		dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CVulkanCommandContext::TransitionTextureLayout(CVulkanTexture* texture, VkImageLayout newLayout)
{
	if (nullptr == texture)
	{
		return;
	}

	const VkImageLayout oldLayout = texture->GetCurrentLayout();
	TransitionImageLayout(texture->GetNativeImage(), oldLayout, newLayout);
	texture->SetCurrentLayout(newLayout);
}
#endif
