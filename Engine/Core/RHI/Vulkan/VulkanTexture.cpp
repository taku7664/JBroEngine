#include "pch.h"
#include "VulkanTexture.h"

CVulkanTexture::CVulkanTexture(const RHITexture2DDesc& desc)
	: m_desc(desc)
{
}

CVulkanTexture::~CVulkanTexture()
{
#if JBRO_PLATFORM_MOBILE
	if (m_device != VK_NULL_HANDLE)
	{
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
#if JBRO_PLATFORM_MOBILE
	handle.Texture = m_image;
	handle.ShaderResourceView = m_imageView;
	handle.RenderTargetView = m_imageView;
#endif
	return handle;
}

#if JBRO_PLATFORM_MOBILE
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
#endif
