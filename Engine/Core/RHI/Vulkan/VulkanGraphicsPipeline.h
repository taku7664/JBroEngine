#pragma once

#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanGraphicsPipeline final : public IRHIGraphicsPipeline
{
public:
	explicit CVulkanGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
	~CVulkanGraphicsPipeline() override;

	const RHIGraphicsPipelineDesc& GetDesc() const override;

#if JBRO_PLATFORM_MOBILE
	void BindNativePipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline);
	VkPipelineLayout GetPipelineLayout() const;
	VkPipeline GetPipeline() const;
#endif

private:
	RHIGraphicsPipelineDesc m_desc;
#if JBRO_PLATFORM_MOBILE
	VkDevice m_device = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
#endif
};
