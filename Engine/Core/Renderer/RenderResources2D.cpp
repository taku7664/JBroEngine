#include "pch.h"
#include "RenderResources2D.h"

CRenderMesh::CRenderMesh(OwnerPtr<IRHIBuffer> vertexBuffer, OwnerPtr<IRHIBuffer> indexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount)
	: m_vertexBuffer(std::move(vertexBuffer))
	, m_indexBuffer(std::move(indexBuffer))
	, m_vertexCount(vertexCount)
	, m_indexCount(indexCount)
{
}

SafePtr<IRHIBuffer> CRenderMesh::GetVertexBuffer() const
{
	return m_vertexBuffer.GetSafePtr();
}

SafePtr<IRHIBuffer> CRenderMesh::GetIndexBuffer() const
{
	return m_indexBuffer.GetSafePtr();
}

std::uint32_t CRenderMesh::GetVertexCount() const
{
	return m_vertexCount;
}

std::uint32_t CRenderMesh::GetIndexCount() const
{
	return m_indexCount;
}

CRenderMaterial::CRenderMaterial(SafePtr<IRHIGraphicsPipeline> pipeline, SafePtr<IRHITexture> texture, SafePtr<IRHISampler> sampler, ERenderQueue queue)
	: m_pipeline(pipeline)
	, m_texture(texture)
	, m_sampler(sampler)
	, m_queue(queue)
{
}

SafePtr<IRHIGraphicsPipeline> CRenderMaterial::GetPipeline() const
{
	return m_pipeline;
}

SafePtr<IRHITexture> CRenderMaterial::GetTexture() const
{
	return m_texture;
}

SafePtr<IRHISampler> CRenderMaterial::GetSampler() const
{
	return m_sampler;
}

ERenderQueue CRenderMaterial::GetRenderQueue() const
{
	return m_queue;
}
