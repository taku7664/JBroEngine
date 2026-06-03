#pragma once

#include "Core/RHI/IRHIProgram.h"
#include "Core/RHI/Vulkan/VulkanCommon.h"

class CVulkanProgram final : public IRHIProgram
{
public:
	CVulkanProgram(ERHIProgramStage stage, ERHIProgramLanguage language);
	~CVulkanProgram() override;

	ERHIProgramStage GetStage() const override;
	ERHIProgramLanguage GetLanguage() const override;

#if JBRO_PLATFORM_MOBILE
	void BindNativeShaderModule(VkDevice device, VkShaderModule shaderModule);
	VkShaderModule GetShaderModule() const;
#endif

private:
	ERHIProgramStage m_stage = ERHIProgramStage::Vertex;
	ERHIProgramLanguage m_language = ERHIProgramLanguage::Unknown;
#if JBRO_PLATFORM_MOBILE
	VkDevice m_device = VK_NULL_HANDLE;
	VkShaderModule m_shaderModule = VK_NULL_HANDLE;
#endif
};
