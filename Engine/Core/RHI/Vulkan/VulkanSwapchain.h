#pragma once

#include "Core/RHI/IRHISwapchain.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

#if JBRO_RHI_VULKAN
#include <vector>
#endif

class CVulkanSwapchain final : public IRHISwapchain
{
public:
	bool Initialize(const RenderSurfaceDesc& surfaceDesc) override;
	void Resize(const RenderSurfaceSize& size) override;
	void Present() override;
	RenderSurfaceSize GetSize() const override;
	float GetPreRotationCosR() const override;
	float GetPreRotationSinR() const override;

	void Finalize();

#if JBRO_RHI_VULKAN
	bool BindNativeSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkQueue presentQueue, std::uint32_t presentQueueFamily, std::uint32_t graphicsQueueFamily);
	void SetPresentWaitSemaphore(VkSemaphore semaphore);
	VkFormat GetFormat() const;
	VkExtent2D GetExtent() const;
	VkRenderPass GetRenderPass() const;
	VkImage GetCurrentImage() const;
	VkImageLayout GetCurrentImageLayout() const;
	void SetCurrentImageLayout(VkImageLayout layout);
	VkFramebuffer GetCurrentFramebuffer() const;
	std::uint32_t GetCurrentImageIndex() const;
	bool AcquireNextImage(VkSemaphore imageAvailableSemaphore);
#endif

private:
#if JBRO_RHI_VULKAN
	void DestroySwapchainObjects();
	bool CreateRenderPass();
	bool CreateSwapchainObjects();
#endif

private:
	RenderSurfaceDesc m_surfaceDesc;
#if JBRO_RHI_VULKAN
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	std::uint32_t m_presentQueueFamily = 0;
	std::uint32_t m_graphicsQueueFamily = 0;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkExtent2D m_extent = {};   // 실제 렌더 버퍼 크기(네이티브 방향, pre-rotation 적용 전)
	VkSurfaceTransformFlagBitsKHR m_preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkSemaphore m_presentWaitSemaphore = VK_NULL_HANDLE;
	std::vector<VkImage> m_images;
	std::vector<VkImageLayout> m_imageLayouts;
	std::vector<VkImageView> m_imageViews;
	std::vector<VkFramebuffer> m_framebuffers;
	std::uint32_t m_currentImageIndex = 0;
#endif
};
