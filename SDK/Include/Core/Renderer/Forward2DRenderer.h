#pragma once

#include "Core/Renderer/IRenderer.h"
#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHISampler.h"

#include "Core/Renderer/RendererTypes.h"
#include <unordered_set>
#include <vector>

class IRHICommandContext;

class CForward2DRenderer final : public IRenderer
{
public:
	bool Initialize(const RendererDesc& desc) override;
	void BeginFrame() override;
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
	struct SpriteConstants
	{
		float TransformRow0[4];
		float TransformRow1[4];
		float Color[4];
		float ViewRow0[4];
		float ViewRow1[4];
	};

	struct SpriteViewConstants
	{
		float ViewRow0[4];
		float ViewRow1[4];
	};

	struct SpriteInstanceData
	{
		float TransformRow0[4];
		float TransformRow1[4];
		float Color[4];
	};

	struct ViewParameters
	{
		float HalfW = 1.0f;
		float HalfH = 1.0f;
		float CosR = 1.0f;
		float SinR = 0.0f;
	};

	struct RenderStateCache
	{
		SafePtr<IRHIGraphicsPipeline> Pipeline;
		SafePtr<IRHIBuffer> VertexBuffer;
		SafePtr<IRHIBuffer> IndexBuffer;
		SafePtr<IRHITexture> Texture;
		SafePtr<IRHISampler> Sampler;
		std::uint32_t VertexStride = 0;
		std::uint32_t VertexOffset = 0;
		SafePtr<IRHIBuffer> InstanceBuffer;
		std::uint32_t InstanceStride = 0;
		std::uint32_t InstanceOffset = 0;
	};

	struct SpriteDrawResources
	{
		SafePtr<IRenderMesh> Mesh;
		SafePtr<IRHIGraphicsPipeline> Pipeline;
		SafePtr<IRHITexture> Texture;
		SafePtr<IRHISampler> Sampler;
	};

	void RenderImpl(IRenderScene& scene, const std::unordered_set<RenderObjectId>* excluded);
	ViewParameters BuildViewParameters() const;
	SpriteDrawResources ResolveSpriteDrawResources(const RenderItem& item) const;
	SpriteConstants BuildSpriteConstants(const RenderItem& item, const ViewParameters& view) const;
	SpriteViewConstants BuildSpriteViewConstants(const ViewParameters& view) const;
	SpriteInstanceData BuildSpriteInstanceData(const RenderItem& item) const;
	SpriteConstants BuildViewportColorConstants(float r, float g, float b, float a) const;
	SafePtr<IRHIBuffer> AcquireSpriteConstantBuffer(IRHICommandContext& commandContext, const SpriteConstants& constants);
	SafePtr<IRHIBuffer> AcquireSpriteViewConstantBuffer(IRHICommandContext& commandContext, const SpriteViewConstants& constants);
	SafePtr<IRHIBuffer> AcquireSpriteInstanceBuffer(IRHICommandContext& commandContext, const SpriteInstanceData* instances, std::uint32_t instanceCount);
	bool DrawSpriteItem(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem& item, const SpriteDrawResources& resources, const ViewParameters& view);
	bool DrawSpriteQuad(IRHICommandContext& commandContext, RenderStateCache& stateCache, SafePtr<IRHIGraphicsPipeline> pipeline, SafePtr<IRenderMesh> mesh, SafePtr<IRHITexture> texture, SafePtr<IRHISampler> sampler, const SpriteConstants& constants);
	bool DrawSpriteBatch(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem* items, std::uint32_t itemCount, const SpriteDrawResources& resources, const ViewParameters& view);
	bool CanBatchSpriteItem(const RenderItem& item, const SpriteDrawResources& resources) const;
	bool CreateSpritePipeline();
	bool CreateSpriteBatchPipeline();
	bool CreateQuadMesh();

private:
	SafePtr<IRHIDevice> m_rhiDevice;
	OwnerPtr<IRHIProgram> m_spriteVertexProgram;
	OwnerPtr<IRHIProgram> m_spritePixelProgram;
	OwnerPtr<IRHIProgram> m_spriteBatchVertexProgram;
	OwnerPtr<IRHIProgram> m_spriteBatchPixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_spritePipeline;
	OwnerPtr<IRHIGraphicsPipeline> m_spriteBatchPipeline;
	OwnerPtr<IRHISampler> m_defaultSampler;
	OwnerPtr<IRenderMesh> m_quadMesh;
	std::vector<OwnerPtr<IRHIBuffer>> m_spriteConstantBuffers;
	std::vector<OwnerPtr<IRHIBuffer>> m_spriteViewConstantBuffers;
	std::vector<OwnerPtr<IRHIBuffer>> m_spriteInstanceBuffers;
	std::vector<SpriteInstanceData> m_spriteBatchInstances;
	std::size_t m_spriteConstantBufferCursor = 0;
	std::size_t m_spriteViewConstantBufferCursor = 0;
	std::size_t m_spriteInstanceBufferCursor = 0;
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
