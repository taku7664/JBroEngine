#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11BlendState;
#endif

class CD3D11GraphicsPipeline final : public IRHIGraphicsPipeline
{
public:
	CD3D11GraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
	~CD3D11GraphicsPipeline() override;

	const RHIGraphicsPipelineDesc& GetDesc() const override;

#if JBRO_PLATFORM_WINDOWS
	void BindNativePipeline(ID3D11InputLayout* inputLayout, ID3D11VertexShader* vertexShader, ID3D11PixelShader* pixelShader);
	void SetBlendState(ID3D11BlendState* blendState);
	ID3D11InputLayout* GetInputLayout() const;
	ID3D11VertexShader* GetVertexShader() const;
	ID3D11PixelShader* GetPixelShader() const;
	ID3D11BlendState* GetBlendState() const;
#endif

private:
	RHIGraphicsPipelineDesc m_desc;
	std::vector<RHIVertexElementDesc> m_vertexElements;
#if JBRO_PLATFORM_WINDOWS
	ID3D11InputLayout*  m_inputLayout  = nullptr;
	ID3D11VertexShader* m_vertexShader = nullptr;
	ID3D11PixelShader*  m_pixelShader  = nullptr;
	ID3D11BlendState*   m_blendState   = nullptr;
#endif
};
