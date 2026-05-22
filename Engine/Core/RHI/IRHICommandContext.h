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
	virtual void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) = 0;
	virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex) = 0;
};
