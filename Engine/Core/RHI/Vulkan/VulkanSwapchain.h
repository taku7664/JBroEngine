#pragma once

#include "Core/RHI/IRHISwapchain.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

#if JBRO_PLATFORM_MOBILE
#include <vector>
#endif

class CVulkanSwapchain final : public IRHISwapchain
{
public:
	bool Initialize(const RenderSurfaceDesc& surfaceDesc) override;
	void Resize(const RenderSurfaceSize& size) override;
	void Present() override;
	RenderSurfaceSize GetSize() const override;

	void Finalize();

#if JBRO_PLATFORM_MOBILE
	bool BindNativeSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkQueue presentQueue, std::uint32_t presentQueueFamily);
	void SetPresentWaitSemaphore(VkSemaphore semaphore);
	VkFormat GetFormat() const;
	VkExtent2D GetExtent() const;
	VkRenderPass GetRenderPass() const;
	VkFramebuffer GetCurrentFramebuffer() const;
	std::uint32_t GetCurrentImageIndex() const;
	bool AcquireNextImage(VkSemaphore imageAvailableSemaphore);
#endif

private:
#if JBRO_PLATFORM_MOBILE
	void DestroySwapchainObjects();
	bool CreateRenderPass();
	bool CreateSwapchainObjects();
#endif

private:
	RenderSurfaceDesc m_surfaceDesc;
#if JBRO_PLATFORM_MOBILE
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	std::uint32_t m_presentQueueFamily = 0;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkExtent2D m_extent = {};
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkSemaphore m_presentWaitSemaphore = VK_NULL_HANDLE;
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;
	std::vector<VkFramebuffer> m_framebuffers;
	std::uint32_t m_currentImageIndex = 0;
#endif
};
