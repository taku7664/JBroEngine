#pragma once

#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanTexture final : public IRHITexture
{
public:
	explicit CVulkanTexture(const RHITexture2DDesc& desc);
	~CVulkanTexture() override;

	const RHITexture2DDesc& GetDesc() const override;
	RHITextureNativeHandle GetNativeHandle() const override;

#if JBRO_RHI_VULKAN
	void BindNativeTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView);
	VkImage GetNativeImage() const;
	VkImageView GetImageView() const;
	VkFramebuffer GetOrCreateFramebuffer(VkRenderPass renderPass);
	VkImageLayout GetCurrentLayout() const;
	void SetCurrentLayout(VkImageLayout layout);
#endif

private:
	RHITexture2DDesc m_desc;
#if JBRO_RHI_VULKAN
	VkDevice m_device = VK_NULL_HANDLE;
	VkImage m_image = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	VkImageView m_imageView = VK_NULL_HANDLE;
	VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkRenderPass m_framebufferRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
#endif
};
