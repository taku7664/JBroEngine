#pragma once

#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanGraphicsPipeline final : public IRHIGraphicsPipeline
{
public:
	explicit CVulkanGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
	~CVulkanGraphicsPipeline() override;

	const RHIGraphicsPipelineDesc& GetDesc() const override;

#if JBRO_RHI_VULKAN
	void BindNativePipeline(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout layout, VkPipeline pipeline);
	VkDescriptorSetLayout GetDescriptorSetLayout() const;
	VkPipelineLayout GetPipelineLayout() const;
	VkPipeline GetPipeline() const;
#endif

private:
	RHIGraphicsPipelineDesc m_desc;
#if JBRO_RHI_VULKAN
	VkDevice m_device = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
#endif
};
