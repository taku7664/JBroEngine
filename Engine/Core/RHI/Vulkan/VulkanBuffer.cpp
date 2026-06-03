#include "pch.h"
#include "VulkanBuffer.h"

CVulkanBuffer::CVulkanBuffer(const RHIBufferDesc& desc)
	: m_desc(desc)
{
}

CVulkanBuffer::~CVulkanBuffer()
{
#if JBRO_PLATFORM_MOBILE
	if (m_device != VK_NULL_HANDLE)
	{
		if (m_buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_device, m_buffer, nullptr);
			m_buffer = VK_NULL_HANDLE;
		}
		if (m_memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
		}
	}
#endif
}

const RHIBufferDesc& CVulkanBuffer::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_MOBILE
void CVulkanBuffer::BindNativeBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
	m_device = device;
	m_buffer = buffer;
	m_memory = memory;
}

VkBuffer CVulkanBuffer::GetNativeBuffer() const
{
	return m_buffer;
}
#endif
