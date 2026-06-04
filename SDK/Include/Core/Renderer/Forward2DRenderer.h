#pragma once

#include "Core/Renderer/IRenderer.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHISampler.h"

#include "Core/Renderer/RendererTypes.h"
#include <unordered_set>

class CForward2DRenderer final : public IRenderer
{
public:
	bool Initialize(const RendererDesc& desc) override;
	void SetRenderTargetSize(const RenderSurfaceSize& size) override;
	void SetViewCamera(float posX, float posY, float orthographicSize) override;
	void SetViewCameraEx(float posX, float posY, float halfW, float halfH, float cosR = 1.0f, float sinR = 0.0f) override;
	// Draw a full-viewport quad in NDC space with the given color (direct overwrite, no blend).
	// Call after BeginRenderPass+SetViewport to clear a sub-viewport area.
	void FillViewportColor(float r, float g, float b, float a) override;
	void Render(IRenderScene& scene) override;
	// 지정 키 집합에 속하는 RenderItem만 렌더링 (포커스 오버레이 / 마스크 패스용).
	void RenderFiltered(IRenderScene& scene, const std::unordered_set<RenderObjectId>& objects);
	// excluded 키 집합에 속하는 RenderItem을 제외하고 전부 렌더링 (에디터 씬뷰 숨김용).
	void RenderExcluding(IRenderScene& scene, const std::unordered_set<RenderObjectId>& excluded);
	void Finalize() override;

	bool CreateGpuResource(IRenderResource& resource) override;
	void DestroyGpuResource(IRenderResource& resource) override;

	SafePtr<IRHIGraphicsPipeline> GetSpritePipeline() const;
	SafePtr<IRHISampler> GetDefaultSampler() const;
	SafePtr<IRenderMesh> GetQuadMesh() const;

private:
	void RenderImpl(IRenderScene& scene, const std::unordered_set<RenderObjectId>* excluded);
	bool CreateSpritePipeline();
	bool CreateQuadMesh();

private:
	SafePtr<IRHIDevice> m_rhiDevice;
	OwnerPtr<IRHIProgram> m_spriteVertexProgram;
	OwnerPtr<IRHIProgram> m_spritePixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_spritePipeline;
	OwnerPtr<IRHISampler> m_defaultSampler;
	OwnerPtr<IRenderMesh> m_quadMesh;
	RenderSurfaceSize m_renderTargetSize;
	// View camera (set per render pass via SetViewCamera / SetViewCameraEx)
	float m_viewCamX      = 0.0f;
	float m_viewCamY      = 0.0f;
	float m_viewCamSize   = 1.0f;  // orthographic half-height; used when m_useExplicitSize == false
	float m_viewCamHalfW  = 1.0f;  // explicit half-width  (stretch mode)
	float m_viewCamHalfH  = 1.0f;  // explicit half-height (stretch mode)
	float m_viewCamCosR   = 1.0f;  // camera rotation cosine (explicit mode)
	float m_viewCamSinR   = 0.0f;  // camera rotation sine   (explicit mode)
	bool  m_useExplicitSize = false; // true → use halfW/halfH/cosR/sinR directly
	// 1×1 white texture used by FillViewportColor
	OwnerPtr<IRHITexture> m_whiteTexture;
	bool m_isInitialized = false;
};
