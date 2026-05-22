#pragma once

#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUGraphicsPipeline final : public IRHIGraphicsPipeline
{
public:
	explicit CWebGPUGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
	~CWebGPUGraphicsPipeline() override;

	const RHIGraphicsPipelineDesc& GetDesc() const override;

#if JBRO_PLATFORM_WEB
	void BindNativePipeline(WGPURenderPipeline pipeline, WGPUBindGroupLayout bindGroupLayout);
	WGPURenderPipeline GetRenderPipeline() const;
	WGPUBindGroupLayout GetBindGroupLayout() const;
#endif

private:
	RHIGraphicsPipelineDesc m_desc;
	std::vector<RHIVertexElementDesc> m_vertexElements;
#if JBRO_PLATFORM_WEB
	WGPURenderPipeline m_pipeline = nullptr;
	WGPUBindGroupLayout m_bindGroupLayout = nullptr;
#endif
};

