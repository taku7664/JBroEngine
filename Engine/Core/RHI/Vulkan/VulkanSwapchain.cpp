#include "pch.h"
#include "VulkanSwapchain.h"

#if JBRO_RHI_VULKAN
#include <algorithm>
#endif

bool CVulkanSwapchain::Initialize(const RenderSurfaceDesc& surfaceDesc)
{
	m_surfaceDesc = surfaceDesc;
	return true;
}

void CVulkanSwapchain::Resize(const RenderSurfaceSize& size)
{
	m_surfaceDesc.Size = size;
#if JBRO_RHI_VULKAN
	CreateSwapchainObjects();
#endif
}

void CVulkanSwapchain::Present()
{
#if JBRO_RHI_VULKAN
	if (m_presentQueue == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE)
	{
		return;
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = m_presentWaitSemaphore != VK_NULL_HANDLE ? 1u : 0u;
	presentInfo.pWaitSemaphores = m_presentWaitSemaphore != VK_NULL_HANDLE ? &m_presentWaitSemaphore : nullptr;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapchain;
	presentInfo.pImageIndices = &m_currentImageIndex;
	vkQueuePresentKHR(m_presentQueue, &presentInfo);
	m_presentWaitSemaphore = VK_NULL_HANDLE;
#endif
}

RenderSurfaceSize CVulkanSwapchain::GetSize() const
{
	return m_surfaceDesc.Size;
}

void CVulkanSwapchain::Finalize()
{
#if JBRO_RHI_VULKAN
	DestroySwapchainObjects();
	if (m_renderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(m_device, m_renderPass, nullptr);
		m_renderPass = VK_NULL_HANDLE;
	}
	m_device = VK_NULL_HANDLE;
	m_physicalDevice = VK_NULL_HANDLE;
	m_instance = VK_NULL_HANDLE;
	m_surface = VK_NULL_HANDLE;
	m_presentQueue = VK_NULL_HANDLE;
#endif
}

#if JBRO_RHI_VULKAN
bool CVulkanSwapchain::BindNativeSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkQueue presentQueue, std::uint32_t presentQueueFamily)
{
	m_instance = instance;
	m_physicalDevice = physicalDevice;
	m_device = device;
	m_surface = surface;
	m_presentQueue = presentQueue;
	m_presentQueueFamily = presentQueueFamily;

	return CreateRenderPass() && CreateSwapchainObjects();
}

void CVulkanSwapchain::SetPresentWaitSemaphore(VkSemaphore semaphore)
{
	m_presentWaitSemaphore = semaphore;
}

VkFormat CVulkanSwapchain::GetFormat() const
{
	return m_format;
}

VkExtent2D CVulkanSwapchain::GetExtent() const
{
	return m_extent;
}

VkRenderPass CVulkanSwapchain::GetRenderPass() const
{
	return m_renderPass;
}

VkFramebuffer CVulkanSwapchain::GetCurrentFramebuffer() const
{
	return m_currentImageIndex < m_framebuffers.size() ? m_framebuffers[m_currentImageIndex] : VK_NULL_HANDLE;
}

VkImage CVulkanSwapchain::GetCurrentImage() const
{
	return m_currentImageIndex < m_images.size() ? m_images[m_currentImageIndex] : VK_NULL_HANDLE;
}

VkImageLayout CVulkanSwapchain::GetCurrentImageLayout() const
{
	return m_currentImageIndex < m_imageLayouts.size() ? m_imageLayouts[m_currentImageIndex] : VK_IMAGE_LAYOUT_UNDEFINED;
}

void CVulkanSwapchain::SetCurrentImageLayout(VkImageLayout layout)
{
	if (m_currentImageIndex < m_imageLayouts.size())
	{
		m_imageLayouts[m_currentImageIndex] = layout;
	}
}

std::uint32_t CVulkanSwapchain::GetCurrentImageIndex() const
{
	return m_currentImageIndex;
}

bool CVulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore)
{
	if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE)
	{
		return false;
	}
	const VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &m_currentImageIndex);
	return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

void CVulkanSwapchain::DestroySwapchainObjects()
{
	if (m_device == VK_NULL_HANDLE)
	{
		return;
	}

	for (VkFramebuffer framebuffer : m_framebuffers)
	{
		vkDestroyFramebuffer(m_device, framebuffer, nullptr);
	}
	m_framebuffers.clear();
	for (VkImageView imageView : m_imageViews)
	{
		vkDestroyImageView(m_device, imageView, nullptr);
	}
	m_imageViews.clear();
	m_images.clear();
	m_imageLayouts.clear();
	if (m_swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
		m_swapchain = VK_NULL_HANDLE;
	}
}

bool CVulkanSwapchain::CreateRenderPass()
{
	if (m_device == VK_NULL_HANDLE)
	{
		return false;
	}
	if (m_renderPass != VK_NULL_HANDLE)
	{
		return true;
	}

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = m_format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef = {};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	return vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) == VK_SUCCESS;
}

bool CVulkanSwapchain::CreateSwapchainObjects()
{
	if (m_physicalDevice == VK_NULL_HANDLE || m_device == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE)
	{
		return false;
	}
	if (m_surfaceDesc.Size.Width <= 0 || m_surfaceDesc.Size.Height <= 0)
	{
		return false;
	}

	vkDeviceWaitIdle(m_device);
	DestroySwapchainObjects();

	VkSurfaceCapabilitiesKHR capabilities = {};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

	std::uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	if (formatCount > 0)
	{
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
	}

	VkSurfaceFormatKHR selectedFormat = formats.empty() ? VkSurfaceFormatKHR{ m_format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
	for (const VkSurfaceFormatKHR& format : formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM)
		{
			selectedFormat = format;
			break;
		}
	}
	m_format = selectedFormat.format;

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		m_extent = capabilities.currentExtent;
	}
	else
	{
		m_extent.width = std::clamp(static_cast<std::uint32_t>(m_surfaceDesc.Size.Width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		m_extent.height = std::clamp(static_cast<std::uint32_t>(m_surfaceDesc.Size.Height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}

	std::uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
	{
		imageCount = capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = m_surface;
	swapchainInfo.minImageCount = imageCount;
	swapchainInfo.imageFormat = m_format;
	swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
	swapchainInfo.imageExtent = m_extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = capabilities.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchainInfo.clipped = VK_TRUE;
	if (vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain) != VK_SUCCESS)
	{
		return false;
	}

	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
	m_images.resize(imageCount);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());
	m_imageLayouts.assign(m_images.size(), VK_IMAGE_LAYOUT_UNDEFINED);

	m_imageViews.resize(m_images.size());
	m_framebuffers.resize(m_images.size());
	for (std::size_t i = 0; i < m_images.size(); ++i)
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_images[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS)
		{
			return false;
		}

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = m_renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &m_imageViews[i];
		framebufferInfo.width = m_extent.width;
		framebufferInfo.height = m_extent.height;
		framebufferInfo.layers = 1;
		if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
		{
			return false;
		}
	}

	return true;
}
#endif
