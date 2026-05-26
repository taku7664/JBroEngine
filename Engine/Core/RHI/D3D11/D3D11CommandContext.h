#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/Platform/RenderSurfaceTypes.h"
#include "Core/RHI/IRHICommandContext.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
#endif

class CD3D11CommandContext final : public IRHICommandContext
{
public:
#if JBRO_PLATFORM_WINDOWS
	void BindNativeContext(ID3D11DeviceContext* deviceContext, ID3D11RenderTargetView* renderTargetView, const RenderSurfaceSize& renderSurfaceSize);
	// Called after swapchain resize to update the stored back-buffer RTV and viewport.
	void UpdateNativeRenderTarget(ID3D11RenderTargetView* renderTargetView, const RenderSurfaceSize& renderSurfaceSize);
#endif

	void BeginFrame() override;
	void BeginRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;
	void EndFrame() override;

	void SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline) override;
	void SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t stride, std::uint32_t offset) override;
	void SetIndexBuffer(SafePtr<IRHIBuffer> buffer) override;
	void SetConstantBuffer(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHIBuffer> buffer) override;
	void UpdateBuffer(SafePtr<IRHIBuffer> buffer, const void* data, std::size_t size) override;
	void SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture) override;
	void SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler) override;
	void SetViewport(float x, float y, float width, float height,
	                 float minDepth = 0.0f, float maxDepth = 1.0f) override;
	void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override;
	void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex) override;

private:
#if JBRO_PLATFORM_WINDOWS
	ID3D11DeviceContext* m_deviceContext = nullptr;
	ID3D11RenderTargetView* m_renderTargetView = nullptr;
	RenderSurfaceSize m_renderSurfaceSize;
#endif
};
