#include "pch.h"
#include "WebGPUGraphicsPipeline.h"

CWebGPUGraphicsPipeline::CWebGPUGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
	: m_desc(desc)
{
	if (desc.VertexElements && desc.VertexElementCount > 0)
	{
		m_vertexElements.assign(desc.VertexElements, desc.VertexElements + desc.VertexElementCount);
		m_desc.VertexElements = m_vertexElements.data();
	}
}

CWebGPUGraphicsPipeline::~CWebGPUGraphicsPipeline()
{
#if JBRO_PLATFORM_WEB
	if (m_pipeline)
	{
		wgpuRenderPipelineRelease(m_pipeline);
		m_pipeline = nullptr;
	}
	if (m_bindGroupLayout)
	{
		wgpuBindGroupLayoutRelease(m_bindGroupLayout);
		m_bindGroupLayout = nullptr;
	}
#endif
}

const RHIGraphicsPipelineDesc& CWebGPUGraphicsPipeline::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WEB
void CWebGPUGraphicsPipeline::BindNativePipeline(WGPURenderPipeline pipeline, WGPUBindGroupLayout bindGroupLayout)
{
	m_pipeline = pipeline;
	m_bindGroupLayout = bindGroupLayout;
}

WGPURenderPipeline CWebGPUGraphicsPipeline::GetRenderPipeline() const
{
	return m_pipeline;
}

WGPUBindGroupLayout CWebGPUGraphicsPipeline::GetBindGroupLayout() const
{
	return m_bindGroupLayout;
}
#endif

