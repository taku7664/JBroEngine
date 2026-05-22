#pragma once

#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHISampler.h"
#include "Core/RHI/IRHITexture.h"

class CRenderMesh final : public IRenderMesh
{
public:
	CRenderMesh(OwnerPtr<IRHIBuffer> vertexBuffer, OwnerPtr<IRHIBuffer> indexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount);

	SafePtr<IRHIBuffer> GetVertexBuffer() const override;
	SafePtr<IRHIBuffer> GetIndexBuffer() const override;
	std::uint32_t GetVertexCount() const override;
	std::uint32_t GetIndexCount() const override;

private:
	OwnerPtr<IRHIBuffer> m_vertexBuffer;
	OwnerPtr<IRHIBuffer> m_indexBuffer;
	std::uint32_t m_vertexCount = 0;
	std::uint32_t m_indexCount = 0;
};

class CRenderMaterial final : public IRenderMaterial
{
public:
	CRenderMaterial(SafePtr<IRHIGraphicsPipeline> pipeline, SafePtr<IRHITexture> texture, SafePtr<IRHISampler> sampler, ERenderQueue queue);

	SafePtr<IRHIGraphicsPipeline> GetPipeline() const override;
	SafePtr<IRHITexture> GetTexture() const override;
	SafePtr<IRHISampler> GetSampler() const override;
	ERenderQueue GetRenderQueue() const override;

private:
	SafePtr<IRHIGraphicsPipeline> m_pipeline;
	SafePtr<IRHITexture> m_texture;
	SafePtr<IRHISampler> m_sampler;
	ERenderQueue m_queue = ERenderQueue::Transparent;
};
