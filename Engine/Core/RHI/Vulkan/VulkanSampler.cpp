#include "pch.h"
#include "VulkanSampler.h"

CVulkanSampler::CVulkanSampler(const RHISamplerDesc& desc)
	: m_desc(desc)
{
}

CVulkanSampler::~CVulkanSampler()
{
#if JBRO_PLATFORM_MOBILE
	if (m_device != VK_NULL_HANDLE && m_sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_sampler, nullptr);
		m_sampler = VK_NULL_HANDLE;
	}
#endif
}

const RHISamplerDesc& CVulkanSampler::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_MOBILE
void CVulkanSampler::BindNativeSampler(VkDevice device, VkSampler sampler)
{
	m_device = device;
	m_sampler = sampler;
}

VkSampler CVulkanSampler::GetNativeSampler() const
{
	return m_sampler;
}
#endif
