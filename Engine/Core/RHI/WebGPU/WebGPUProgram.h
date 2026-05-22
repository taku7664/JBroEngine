#pragma once

#include "Core/RHI/IRHIProgram.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUProgram final : public IRHIProgram
{
public:
	CWebGPUProgram(ERHIProgramStage stage, ERHIProgramLanguage language);
	~CWebGPUProgram() override;

	ERHIProgramStage GetStage() const override;
	ERHIProgramLanguage GetLanguage() const override;

#if JBRO_PLATFORM_WEB
	void BindNativeShaderModule(WGPUShaderModule shaderModule);
	WGPUShaderModule GetShaderModule() const;
#endif

private:
	ERHIProgramStage m_stage = ERHIProgramStage::Vertex;
	ERHIProgramLanguage m_language = ERHIProgramLanguage::Unknown;
#if JBRO_PLATFORM_WEB
	WGPUShaderModule m_shaderModule = nullptr;
#endif
};

