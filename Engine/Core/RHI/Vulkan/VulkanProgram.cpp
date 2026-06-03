#include "pch.h"
#include "VulkanProgram.h"

CVulkanProgram::CVulkanProgram(ERHIProgramStage stage, ERHIProgramLanguage language)
	: m_stage(stage)
	, m_language(language)
{
}

CVulkanProgram::~CVulkanProgram()
{
#if JBRO_PLATFORM_MOBILE
	if (m_device != VK_NULL_HANDLE && m_shaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(m_device, m_shaderModule, nullptr);
		m_shaderModule = VK_NULL_HANDLE;
	}
#endif
}

ERHIProgramStage CVulkanProgram::GetStage() const
{
	return m_stage;
}

ERHIProgramLanguage CVulkanProgram::GetLanguage() const
{
	return m_language;
}

#if JBRO_PLATFORM_MOBILE
void CVulkanProgram::BindNativeShaderModule(VkDevice device, VkShaderModule shaderModule)
{
	m_device = device;
	m_shaderModule = shaderModule;
}

VkShaderModule CVulkanProgram::GetShaderModule() const
{
	return m_shaderModule;
}
#endif
