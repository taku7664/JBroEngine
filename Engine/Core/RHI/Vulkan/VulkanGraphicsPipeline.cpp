#include "pch.h"
#include "VulkanGraphicsPipeline.h"

CVulkanGraphicsPipeline::CVulkanGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
	: m_desc(desc)
{
}

CVulkanGraphicsPipeline::~CVulkanGraphicsPipeline()
{
#if JBRO_RHI_VULKAN
	if (m_device != VK_NULL_HANDLE)
	{
		if (m_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_device, m_pipeline, nullptr);
			m_pipeline = VK_NULL_HANDLE;
		}
		if (m_pipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
		}
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
			m_descriptorSetLayout = VK_NULL_HANDLE;
		}
	}
#endif
}

const RHIGraphicsPipelineDesc& CVulkanGraphicsPipeline::GetDesc() const
{
	return m_desc;
}

#if JBRO_RHI_VULKAN
void CVulkanGraphicsPipeline::BindNativePipeline(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout layout, VkPipeline pipeline)
{
	m_device = device;
	m_descriptorSetLayout = descriptorSetLayout;
	m_pipelineLayout = layout;
	m_pipeline = pipeline;
}

VkDescriptorSetLayout CVulkanGraphicsPipeline::GetDescriptorSetLayout() const
{
	return m_descriptorSetLayout;
}

VkPipelineLayout CVulkanGraphicsPipeline::GetPipelineLayout() const
{
	return m_pipelineLayout;
}

VkPipeline CVulkanGraphicsPipeline::GetPipeline() const
{
	return m_pipeline;
}
#endif
