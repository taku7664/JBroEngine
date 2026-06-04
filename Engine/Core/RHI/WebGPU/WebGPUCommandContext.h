#pragma once

#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

#include <vector>

class CWebGPUSwapchain;
class CWebGPUGraphicsPipeline;

class CWebGPUCommandContext final : public IRHICommandContext
{
public:
	~CWebGPUCommandContext() override;

#if JBRO_PLATFORM_WEB
	void BindNativeContext(WGPUDevice device, WGPUQueue queue, CWebGPUSwapchain* swapchain);
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
#if JBRO_PLATFORM_WEB
	struct BindGroupCacheEntry
	{
		SafePtr<IRHIGraphicsPipeline> Pipeline;
		SafePtr<IRHIBuffer> ConstantBuffer;
		SafePtr<IRHITexture> Texture;
		SafePtr<IRHISampler> Sampler;
		WGPUBindGroup BindGroup = nullptr;
	};

	void ReleaseFrameObjects();
	void ReleaseBindGroupCache();
	void PruneInvalidBindGroups();
	WGPUBindGroup GetOrCreateCurrentBindGroup();
#endif

private:
#if JBRO_PLATFORM_WEB
	WGPUDevice m_device = nullptr;
	WGPUQueue m_queue = nullptr;
	CWebGPUSwapchain* m_swapchain = nullptr;
	WGPUCommandEncoder m_commandEncoder = nullptr;
	WGPURenderPassEncoder m_renderPass = nullptr;
	WGPUCommandBuffer m_pendingCommandBuffer = nullptr;
	SafePtr<IRHIGraphicsPipeline> m_currentPipelineHandle;
	CWebGPUGraphicsPipeline* m_currentPipeline = nullptr;
	SafePtr<IRHIBuffer> m_constantBuffer;
	SafePtr<IRHITexture> m_texture;
	SafePtr<IRHISampler> m_sampler;
	std::vector<BindGroupCacheEntry> m_bindGroupCache;
	std::size_t m_bindGroupCacheCursor = 0;
#endif
};
