#pragma once

#include <cstdint>

#include "Utillity/SafePtr.h"
#include "Core/RHI/RHICommandTypes.h"

class IRHIBuffer;
class IRHIGraphicsPipeline;
class IRHITexture;
class IRHISampler;

class IRHICommandContext : public EnableSafeFromThis<IRHICommandContext>
{
public:
	virtual ~IRHICommandContext() = default;

public:
	virtual void BeginFrame() = 0;
	virtual void BeginRenderPass(const RenderPassDesc& desc) = 0;
	virtual void EndRenderPass() = 0;
	virtual void EndFrame() = 0;

	virtual void SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline) = 0;
	virtual void SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t stride, std::uint32_t offset) = 0;
	virtual void SetIndexBuffer(SafePtr<IRHIBuffer> buffer) = 0;
	virtual void SetConstantBuffer(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHIBuffer> buffer) = 0;
	virtual void SetTexture(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHITexture> texture) = 0;
	virtual void SetSampler(ERHIProgramStage stage, std::uint32_t slot, SafePtr<IRHISampler> sampler) = 0;
	// Override the current viewport after BeginRenderPass.
	// Coordinates are in pixels; minDepth/maxDepth default to [0, 1].
	virtual void SetViewport(float x, float y, float width, float height,
	                         float minDepth = 0.0f, float maxDepth = 1.0f) {}
	virtual void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) = 0;
	virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex) = 0;
};
