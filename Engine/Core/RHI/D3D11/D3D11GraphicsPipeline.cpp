#include "pch.h"
#include "D3D11GraphicsPipeline.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#endif

CD3D11GraphicsPipeline::CD3D11GraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
	: m_desc(desc)
{
	if (desc.VertexElements && desc.VertexElementCount > 0)
	{
		m_vertexElements.assign(desc.VertexElements, desc.VertexElements + desc.VertexElementCount);
		m_desc.VertexElements = m_vertexElements.data();
	}
}

CD3D11GraphicsPipeline::~CD3D11GraphicsPipeline()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_inputLayout)
	{
		m_inputLayout->Release();
		m_inputLayout = nullptr;
	}
	if (m_vertexShader)
	{
		m_vertexShader->Release();
		m_vertexShader = nullptr;
	}
	if (m_pixelShader)
	{
		m_pixelShader->Release();
		m_pixelShader = nullptr;
	}
#endif
}

const RHIGraphicsPipelineDesc& CD3D11GraphicsPipeline::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11GraphicsPipeline::BindNativePipeline(ID3D11InputLayout* inputLayout, ID3D11VertexShader* vertexShader, ID3D11PixelShader* pixelShader)
{
	m_inputLayout = inputLayout;
	m_vertexShader = vertexShader;
	m_pixelShader = pixelShader;
}

ID3D11InputLayout* CD3D11GraphicsPipeline::GetInputLayout() const
{
	return m_inputLayout;
}

ID3D11VertexShader* CD3D11GraphicsPipeline::GetVertexShader() const
{
	return m_vertexShader;
}

ID3D11PixelShader* CD3D11GraphicsPipeline::GetPixelShader() const
{
	return m_pixelShader;
}
#endif
