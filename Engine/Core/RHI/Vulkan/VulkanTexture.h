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

#if JBRO_PLATFORM_MOBILE
	void BindNativeTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView);
	VkImage GetNativeImage() const;
	VkImageView GetImageView() const;
#endif

private:
	RHITexture2DDesc m_desc;
#if JBRO_PLATFORM_MOBILE
	VkDevice m_device = VK_NULL_HANDLE;
	VkImage m_image = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	VkImageView m_imageView = VK_NULL_HANDLE;
#endif
};
