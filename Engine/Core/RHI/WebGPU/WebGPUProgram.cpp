#include "pch.h"
#include "WebGPUProgram.h"

CWebGPUProgram::CWebGPUProgram(ERHIProgramStage stage, ERHIProgramLanguage language)
	: m_stage(stage)
	, m_language(language)
{
}

CWebGPUProgram::~CWebGPUProgram()
{
#if JBRO_PLATFORM_WEB
	if (m_shaderModule)
	{
		wgpuShaderModuleRelease(m_shaderModule);
		m_shaderModule = nullptr;
	}
#endif
}

ERHIProgramStage CWebGPUProgram::GetStage() const
{
	return m_stage;
}

ERHIProgramLanguage CWebGPUProgram::GetLanguage() const
{
	return m_language;
}

#if JBRO_PLATFORM_WEB
void CWebGPUProgram::BindNativeShaderModule(WGPUShaderModule shaderModule)
{
	m_shaderModule = shaderModule;
}

WGPUShaderModule CWebGPUProgram::GetShaderModule() const
{
	return m_shaderModule;
}
#endif

