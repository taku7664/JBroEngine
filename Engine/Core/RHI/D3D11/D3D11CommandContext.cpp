#include "pch.h"
#include "D3D11CommandContext.h"

#include "Core/RHI/D3D11/D3D11Buffer.h"
#include "Core/RHI/D3D11/D3D11GraphicsPipeline.h"
#include "Core/RHI/D3D11/D3D11Sampler.h"
#include "Core/RHI/D3D11/D3D11Texture.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>

namespace
{
	D3D11_PRIMITIVE_TOPOLOGY ToD3DTopology(ERHIPrimitiveTopology topology)
	{
		switch (topology)
		{
		case ERHIPrimitiveTopology::TriangleStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case ERHIPrimitiveTopology::LineList:
			return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case ERHIPrimitiveTopology::LineStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		default:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}
}

void CD3D11CommandContext::BindNativeContext(ID3D11DeviceContext* deviceContext, ID3D11RenderTargetView* renderTargetView, const RenderSurfaceSize& renderSurfaceSize)
{
	m_deviceContext = deviceContext;
	m_renderTargetView = renderTargetView;
	m_renderSurfaceSize = renderSurfaceSize;
}

void CD3D11CommandContext::UpdateNativeRenderTarget(ID3D11RenderTargetView* renderTargetView, const RenderSurfaceSize& renderSurfaceSize)
{
	m_renderTargetView  = renderTargetView;
	m_renderSurfaceSize = renderSurfaceSize;
}
#endif

void CD3D11CommandContext::BeginFrame()
{
}

void CD3D11CommandContext::BeginRenderPass(const RenderPassDesc& desc)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || nullptr == m_renderTargetView)
	{
		return;
	}

	ID3D11RenderTargetView* renderTargetView = m_renderTargetView;
	std::uint32_t width = m_renderSurfaceSize.Width;
	std::uint32_t height = m_renderSurfaceSize.Height;
	if (desc.ColorAttachment.Target)
	{
		CD3D11Texture* targetTexture = static_cast<CD3D11Texture*>(desc.ColorAttachment.Target.TryGet());
		if (targetTexture && targetTexture->GetRenderTargetView())
		{
			renderTargetView = targetTexture->GetRenderTargetView();
			width = targetTexture->GetDesc().Width;
			height = targetTexture->GetDesc().Height;
		}
	}

	m_deviceContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_deviceContext->RSSetViewports(1, &viewport);

	if (ERHILoadOp::Clear == desc.ColorAttachment.LoadOp)
	{
		const float clearColor[4] = {
			desc.ColorAttachment.ClearColor.R,
			desc.ColorAttachment.ClearColor.G,
			desc.ColorAttachment.ClearColor.B,
			desc.ColorAttachment.ClearColor.A,
		};
		m_deviceContext->ClearRenderTargetView(renderTargetView, clearColor);
	}
#else
	(void)desc;
#endif
}

void CD3D11CommandContext::EndRenderPass()
{
}

void CD3D11CommandContext::EndFrame()
{
}

void CD3D11CommandContext::SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || false == pipeline.IsValid())
	{
		return;
	}

	CD3D11GraphicsPipeline* d3dPipeline = static_cast<CD3D11GraphicsPipeline*>(pipeline.TryGet());
	if (nullptr == d3dPipeline)
	{
		return;
	}

	m_deviceContext->IASetInputLayout(d3dPipeline->GetInputLayout());
	m_deviceContext->IASetPrimitiveTopology(ToD3DTopology(d3dPipeline->GetDesc().PrimitiveTopology));
	m_deviceContext->VSSetShader(d3dPipeline->GetVertexShader(), nullptr, 0);
	m_deviceContext->PSSetShader(d3dPipeline->GetPixelShader(), nullptr, 0);
#else
	(void)pipeline;
#endif
}

void CD3D11CommandContext::SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t stride, std::uint32_t offset)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || !buffer)
	{
		return;
	}

	CD3D11Buffer* d3dBuffer = static_cast<CD3D11Buffer*>(buffer.TryGet());
	if (nullptr == d3dBuffer)
	{
		return;
	}

	ID3D11Buffer* nativeBuffer = d3dBuffer->GetNativeBuffer();
	UINT nativeStride = stride;
	UINT nativeOffset = offset;
	m_deviceContext->IASetVertexBuffers(slot, 1, &nativeBuffer, &nativeStride, &nativeOffset);
#else
	(void)slot;
	(void)buffer;
	(void)stride;
	(void)offset;
#endif
}

void CD3D11CommandContext::SetIndexBuffer(SafePtr<IRHIBuffer> buffer)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || false == buffer.IsValid())
	{
		return;
	}

	CD3D11Buffer* d3dBuffer = static_cast<CD3D11Buffer*>(buffer.TryGet());
	if (nullptr == d3dBuffer)
	{
		return;
	}

	m_deviceContext->IASetIndexBuffer(d3dBuffer->GetNativeBuffer(), DXGI_FORMAT_R32_UINT, 0);
#else
	(void)buffer;
#endif
}

void CD3D11CommandContext::SetConstantBuffer(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHIBuffer> buffer)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || false == buffer.IsValid())
	{
		return;
	}

	CD3D11Buffer* d3dBuffer = static_cast<CD3D11Buffer*>(buffer.TryGet());
	if (nullptr == d3dBuffer)
	{
		return;
	}

	ID3D11Buffer* nativeBuffer = d3dBuffer->GetNativeBuffer();
	if (ERHIProgramStage::Pixel == stage)
	{
		m_deviceContext->PSSetConstantBuffers(slot, 1, &nativeBuffer);
	}
	else
	{
		m_deviceContext->VSSetConstantBuffers(slot, 1, &nativeBuffer);
	}
#else
	(void)stage;
	(void)slot;
	(void)buffer;
#endif
}

void CD3D11CommandContext::SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || false == texture.IsValid())
	{
		return;
	}

	CD3D11Texture* d3dTexture = static_cast<CD3D11Texture*>(texture.TryGet());
	if (nullptr == d3dTexture)
	{
		return;
	}

	ID3D11ShaderResourceView* shaderResourceView = d3dTexture->GetShaderResourceView();
	if (ERHIProgramStage::Pixel == stage)
	{
		m_deviceContext->PSSetShaderResources(slot, 1, &shaderResourceView);
	}
#else
	(void)stage;
	(void)slot;
	(void)texture;
#endif
}

void CD3D11CommandContext::SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext || false == sampler.IsValid())
	{
		return;
	}

	CD3D11Sampler* d3dSampler = static_cast<CD3D11Sampler*>(sampler.TryGet());
	if (nullptr == d3dSampler)
	{
		return;
	}

	ID3D11SamplerState* nativeSampler = d3dSampler->GetNativeSampler();
	if (ERHIProgramStage::Pixel == stage)
	{
		m_deviceContext->PSSetSamplers(slot, 1, &nativeSampler);
	}
#else
	(void)stage;
	(void)slot;
	(void)sampler;
#endif
}

void CD3D11CommandContext::SetViewport(float x, float y, float width, float height,
                                        float minDepth, float maxDepth)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext)
	{
		return;
	}
	D3D11_VIEWPORT vp = {};
	vp.TopLeftX = x;
	vp.TopLeftY = y;
	vp.Width    = (width  > 0.0f) ? width  : 1.0f;
	vp.Height   = (height > 0.0f) ? height : 1.0f;
	vp.MinDepth = minDepth;
	vp.MaxDepth = maxDepth;
	m_deviceContext->RSSetViewports(1, &vp);
#else
	(void)x; (void)y; (void)width; (void)height; (void)minDepth; (void)maxDepth;
#endif
}

void CD3D11CommandContext::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext)
	{
		return;
	}

	m_deviceContext->Draw(vertexCount, firstVertex);
#else
	(void)vertexCount;
	(void)firstVertex;
#endif
}

void CD3D11CommandContext::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_deviceContext)
	{
		return;
	}

	m_deviceContext->DrawIndexed(indexCount, firstIndex, baseVertex);
#else
	(void)indexCount;
	(void)firstIndex;
	(void)baseVertex;
#endif
}
