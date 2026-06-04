#pragma once

#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

#if JBRO_RHI_VULKAN
#include <functional>
#include <vector>
#endif

class CVulkanRHIDevice final : public IRHIDevice
{
public:
	bool Initialize(const RHIDesc& desc) override;
	void BeginFrame() override;
	void EndFrame() override;
	void Finalize() override;
	void HandleSurfaceResize(const RenderSurfaceSize& size) override;

	OwnerPtr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData) override;
	OwnerPtr<IRHITexture> CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData) override;
	OwnerPtr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) override;
	OwnerPtr<IRHIProgram> CreateProgram(const RHIProgramDesc& desc) override;
	OwnerPtr<IRHIGraphicsPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;

	SafePtr<IRHISwapchain> GetSwapchain() const override;
	SafePtr<IRHICommandContext> GetImmediateCommandContext() const override;
	RHINativeDeviceDesc GetNativeDeviceDesc() const override;

	ERHIApi GetApi() const override;
	const char* GetName() const override;

private:
#if JBRO_RHI_VULKAN
	bool CreateInstance();
	bool CreateDebugMessenger();
	bool CreateSurface();
	bool SelectPhysicalDevice();
	bool CreateLogicalDevice();
	bool CreateCommandPool();
	void DestroyVulkanObjects();
	std::uint32_t FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
	bool CreateNativeBuffer(std::size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;
	bool SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const;
	bool TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;
	bool CopyBufferToImage(VkBuffer buffer, VkImage image, std::uint32_t width, std::uint32_t height) const;
#endif

private:
	RHIDesc m_desc;
	OwnerPtr<IRHISwapchain> m_rhiSwapchain;
	OwnerPtr<IRHICommandContext> m_immediateCommandContext;
	bool m_isInitialized = false;
#if JBRO_RHI_VULKAN
	VkInstance m_instance = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
	std::uint32_t m_graphicsQueueFamily = UINT32_MAX;
	std::uint32_t m_presentQueueFamily = UINT32_MAX;
	bool m_enableValidationLayer = false;
	bool m_enableDebugUtils = false;
#endif
};
