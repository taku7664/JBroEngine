#include "pch.h"
#include "VulkanCommandContext.h"

#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanSwapchain.h"

#if JBRO_PLATFORM_MOBILE
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
}
#endif

void CVulkanCommandContext::BeginFrame()
{
#if JBRO_PLATFORM_MOBILE
	if (m_device == VK_NULL_HANDLE || m_commandBuffer == VK_NULL_HANDLE || nullptr == m_swapchain)
	{
		return;
	}

	vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
	vkResetFences(m_device, 1, &m_inFlightFence);
	vkResetCommandBuffer(m_commandBuffer, 0);
	m_swapchain->AcquireNextImage(m_imageAvailableSemaphore);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
#endif
}

void CVulkanCommandContext::BeginRenderPass(const RenderPassDesc& desc)
{
#if JBRO_PLATFORM_MOBILE
	if (m_commandBuffer == VK_NULL_HANDLE || nullptr == m_swapchain || m_swapchain->GetRenderPass() == VK_NULL_HANDLE)
	{
		return;
	}

	VkClearValue clearValue = {};
	clearValue.color.float32[0] = desc.ColorAttachment.ClearColor.R;
	clearValue.color.float32[1] = desc.ColorAttachment.ClearColor.G;
	clearValue.color.float32[2] = desc.ColorAttachment.ClearColor.B;
	clearValue.color.float32[3] = desc.ColorAttachment.ClearColor.A;

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = m_swapchain->GetRenderPass();
	passInfo.framebuffer = m_swapchain->GetCurrentFramebuffer();
	passInfo.renderArea.offset = { 0, 0 };
	passInfo.renderArea.extent = m_swapchain->GetExtent();
	passInfo.clearValueCount = 1;
	passInfo.pClearValues = &clearValue;
	vkCmdBeginRenderPass(m_commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
	m_isRenderPassOpen = true;

	SetViewport(0.0f, 0.0f, static_cast<float>(m_swapchain->GetExtent().width), static_cast<float>(m_swapchain->GetExtent().height));
#else
	(void)desc;
#endif
}

void CVulkanCommandContext::EndRenderPass()
{
#if JBRO_PLATFORM_MOBILE
	if (m_commandBuffer != VK_NULL_HANDLE && m_isRenderPassOpen)
	{
		vkCmdEndRenderPass(m_commandBuffer);
		m_isRenderPassOpen = false;
	}
#endif
}

void CVulkanCommandContext::EndFrame()
{
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
	if (m_commandBuffer == VK_NULL_HANDLE || false == pipeline.IsValid())
	{
		return;
	}

	CVulkanGraphicsPipeline* vkPipeline = static_cast<CVulkanGraphicsPipeline*>(pipeline.TryGet());
	if (vkPipeline && vkPipeline->GetPipeline() != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetPipeline());
	}
#else
	(void)pipeline;
#endif
}

void CVulkanCommandContext::SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t, std::uint32_t offset)
{
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
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
	(void)stage;
	(void)slot;
	(void)buffer;
}

void CVulkanCommandContext::UpdateBuffer(SafePtr<IRHIBuffer> buffer, const void* data, std::size_t size)
{
	(void)buffer;
	(void)data;
	(void)size;
}

void CVulkanCommandContext::SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture)
{
	(void)stage;
	(void)slot;
	(void)texture;
}

void CVulkanCommandContext::SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler)
{
	(void)stage;
	(void)slot;
	(void)sampler;
}

void CVulkanCommandContext::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
	if (m_commandBuffer != VK_NULL_HANDLE)
	{
		vkCmdDraw(m_commandBuffer, vertexCount, 1, firstVertex, 0);
	}
#else
	(void)vertexCount;
	(void)firstVertex;
#endif
}

void CVulkanCommandContext::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex)
{
#if JBRO_PLATFORM_MOBILE
	if (m_commandBuffer != VK_NULL_HANDLE)
	{
		vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, firstIndex, baseVertex, 0);
	}
#else
	(void)indexCount;
	(void)firstIndex;
	(void)baseVertex;
#endif
}
