#pragma once

#include "Core/RHI/IRHISampler.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanSampler final : public IRHISampler
{
public:
	explicit CVulkanSampler(const RHISamplerDesc& desc);
	~CVulkanSampler() override;

	const RHISamplerDesc& GetDesc() const override;

#if JBRO_PLATFORM_MOBILE
	void BindNativeSampler(VkDevice device, VkSampler sampler);
	VkSampler GetNativeSampler() const;
#endif

private:
	RHISamplerDesc m_desc;
#if JBRO_PLATFORM_MOBILE
	VkDevice m_device = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;
#endif
};
