#pragma once

#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanBuffer final : public IRHIBuffer
{
public:
	explicit CVulkanBuffer(const RHIBufferDesc& desc);
	~CVulkanBuffer() override;

	const RHIBufferDesc& GetDesc() const override;

#if JBRO_PLATFORM_MOBILE
	void BindNativeBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);
	VkBuffer GetNativeBuffer() const;
#endif

private:
	RHIBufferDesc m_desc;
#if JBRO_PLATFORM_MOBILE
	VkDevice m_device = VK_NULL_HANDLE;
	VkBuffer m_buffer = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
#endif
};
