#pragma once

#include "Core/Renderer/IRenderer.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHISampler.h"

class CForward2DRenderer final : public IRenderer
{
public:
	bool Initialize(const RendererDesc& desc) override;
	void Render(IRenderScene& scene) override;
	void Finalize() override;

	bool CreateGpuResource(IRenderResource& resource) override;
	void DestroyGpuResource(IRenderResource& resource) override;

	SafePtr<IRHIGraphicsPipeline> GetSpritePipeline() const;
	SafePtr<IRHISampler> GetDefaultSampler() const;
	SafePtr<IRenderMesh> GetQuadMesh() const;

private:
	bool CreateSpritePipeline();
	bool CreateQuadMesh();

private:
	SafePtr<IRHIDevice> m_rhiDevice;
	OwnerPtr<IRHIProgram> m_spriteVertexProgram;
	OwnerPtr<IRHIProgram> m_spritePixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_spritePipeline;
	OwnerPtr<IRHISampler> m_defaultSampler;
	OwnerPtr<IRenderMesh> m_quadMesh;
	bool m_isInitialized = false;
};
