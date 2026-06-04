#include "pch.h"
#include "VulkanTexture.h"

CVulkanTexture::CVulkanTexture(const RHITexture2DDesc& desc)
	: m_desc(desc)
{
}

CVulkanTexture::~CVulkanTexture()
{
#if JBRO_RHI_VULKAN
	if (m_device != VK_NULL_HANDLE)
	{
		if (m_framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
			m_framebuffer = VK_NULL_HANDLE;
			m_framebufferRenderPass = VK_NULL_HANDLE;
		}
		if (m_imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_device, m_imageView, nullptr);
			m_imageView = VK_NULL_HANDLE;
		}
		if (m_image != VK_NULL_HANDLE)
		{
			vkDestroyImage(m_device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
		}
		if (m_memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
		}
	}
#endif
}

const RHITexture2DDesc& CVulkanTexture::GetDesc() const
{
	return m_desc;
}

RHITextureNativeHandle CVulkanTexture::GetNativeHandle() const
{
	RHITextureNativeHandle handle;
#if JBRO_RHI_VULKAN
	handle.Texture = m_image;
	handle.ShaderResourceView = m_imageView;
	handle.RenderTargetView = m_imageView;
#endif
	return handle;
}

#if JBRO_RHI_VULKAN
void CVulkanTexture::BindNativeTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView)
{
	m_device = device;
	m_image = image;
	m_memory = memory;
	m_imageView = imageView;
}

VkImage CVulkanTexture::GetNativeImage() const
{
	return m_image;
}

VkImageView CVulkanTexture::GetImageView() const
{
	return m_imageView;
}

VkFramebuffer CVulkanTexture::GetOrCreateFramebuffer(VkRenderPass renderPass)
{
	if (m_device == VK_NULL_HANDLE || m_imageView == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
	{
		return VK_NULL_HANDLE;
	}
	if (m_framebuffer != VK_NULL_HANDLE && m_framebufferRenderPass == renderPass)
	{
		return m_framebuffer;
	}
	if (m_framebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
		m_framebuffer = VK_NULL_HANDLE;
	}

	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &m_imageView;
	framebufferInfo.width = m_desc.Width;
	framebufferInfo.height = m_desc.Height;
	framebufferInfo.layers = 1;
	if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS)
	{
		m_framebufferRenderPass = VK_NULL_HANDLE;
		return VK_NULL_HANDLE;
	}

	m_framebufferRenderPass = renderPass;
	return m_framebuffer;
}

VkImageLayout CVulkanTexture::GetCurrentLayout() const
{
	return m_currentLayout;
}

void CVulkanTexture::SetCurrentLayout(VkImageLayout layout)
{
	m_currentLayout = layout;
}
#endif
