#pragma once

#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

#include <vector>

class CVulkanSwapchain;
class CVulkanGraphicsPipeline;
class CVulkanBuffer;
class CVulkanTexture;
class CVulkanSampler;

class CVulkanCommandContext final : public IRHICommandContext
{
public:
	~CVulkanCommandContext() override;

#if JBRO_RHI_VULKAN
	void BindNativeContext(VkDevice device, VkQueue graphicsQueue, VkCommandPool commandPool, CVulkanSwapchain* swapchain);
#endif

	void BeginFrame() override;
	void BeginRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;
	void EndFrame() override;

	void SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline) override;
	void SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t stride, std::uint32_t offset) override;
	void SetIndexBuffer(SafePtr<IRHIBuffer> buffer) override;
	void SetConstantBuffer(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHIBuffer> buffer) override;
	void UpdateBuffer(SafePtr<IRHIBuffer> buffer, const void* data, std::size_t size) override;
	void SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture) override;
	void SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler) override;
	void SetViewport(float x, float y, float width, float height,
	                 float minDepth = 0.0f, float maxDepth = 1.0f) override;
	void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override;
	void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex) override;
	void DrawIndexedInstanced(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t baseVertex, std::uint32_t firstInstance) override;

private:
#if JBRO_RHI_VULKAN
	void DestroyFrameSync();
	VkDescriptorPool CreateDescriptorPool(std::uint32_t maxSets);
	bool AllocateDescriptorSet(VkDescriptorSetLayout setLayout, VkDescriptorSet* outDescriptorSet);
	void BindPendingDescriptors();
	void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
	void TransitionTextureLayout(CVulkanTexture* texture, VkImageLayout newLayout);
#endif

private:
#if JBRO_RHI_VULKAN
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	CVulkanSwapchain* m_swapchain = nullptr;
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	std::vector<VkDescriptorPool> m_descriptorPools;
	std::size_t m_activeDescriptorPoolIndex = 0;
	CVulkanGraphicsPipeline* m_currentPipeline = nullptr;
	SafePtr<IRHIBuffer> m_boundConstantBuffer;
	SafePtr<IRHITexture> m_boundTexture;
	SafePtr<IRHISampler> m_boundSampler;
	CVulkanTexture* m_activeRenderTarget = nullptr;
	VkImage m_activeRenderImage = VK_NULL_HANDLE;
	VkImageLayout m_activeRenderInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkExtent2D m_activeRenderExtent = {};
	bool m_isRenderPassOpen = false;
#endif
};
