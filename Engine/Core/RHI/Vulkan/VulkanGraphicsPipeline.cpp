#include "pch.h"
#include "VulkanGraphicsPipeline.h"

CVulkanGraphicsPipeline::CVulkanGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
	: m_desc(desc)
{
}

CVulkanGraphicsPipeline::~CVulkanGraphicsPipeline()
{
#if JBRO_PLATFORM_MOBILE
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
	}
#endif
}

const RHIGraphicsPipelineDesc& CVulkanGraphicsPipeline::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_MOBILE
void CVulkanGraphicsPipeline::BindNativePipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline)
{
	m_device = device;
	m_pipelineLayout = layout;
	m_pipeline = pipeline;
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
