#pragma once

#include "Core/Renderer/IRenderer.h"
#include "Core/Renderer/RenderWeave/RenderWeaveGraph.h"
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
	void SetSurfacePreRotation(float cosR, float sinR) override;
	// Draw a full-viewport quad in NDC space with the given color (direct overwrite, no blend).
	// Call after BeginRenderPass+SetViewport to clear a sub-viewport area.
	void FillViewportColor(float r, float g, float b, float a) override;
	void Render(IRenderScene& scene) override;
	RenderCullingStats GetLastCullingStats() const override;
	// 지정 키 집합에 속하는 RenderItem만 렌더링 (포커스 오버레이 / 마스크 패스용).
	void RenderFiltered(IRenderScene& scene, const std::unordered_set<RenderObjectId>& objects);
	// excluded 키 집합에 속하는 RenderItem을 제외하고 전부 렌더링 (에디터 씬뷰 숨김용).
	void RenderExcluding(IRenderScene& scene, const std::unordered_set<RenderObjectId>& excluded);
	void Finalize() override;

	bool CreateGpuResource(IRenderResource& resource) override;
	void DestroyGpuResource(IRenderResource& resource) override;

	SafePtr<IRHIGraphicsPipeline> GetSpritePipeline() const;
	SafePtr<IRHIGraphicsPipeline> GetTextPipeline() const;
	SafePtr<IRHISampler> GetDefaultSampler() const;
	SafePtr<IRenderMesh> GetQuadMesh() const;
	SafePtr<IRHITexture> GetWhiteTexture() const;

	// ── RenderWeave: 렌더그래프 RT 풀 + 풀스크린 blit ────────────────────────
	// transient RT 대여 풀. RenderGameCameraStack 이 프레임마다 RWGraph 를 구성해 쓴다.
	RWTexturePool& GetRenderWeavePool();
	// src 텍스처를 현재 바인딩된 렌더패스에 풀스크린으로 복사한다(전용 no-blend 파이프라인).
	void BlitFullscreen(IRHICommandContext& commandContext, SafePtr<IRHITexture> src);

	// ── RenderWeave 라이팅 ──────────────────────────────────────────────────────
	// 라이트 1개를 LightMap 에 가산 누적한다(현재 SetViewCameraEx 로 설정된 뷰 기준).
	// type: 0=Directional(뷰포트 균일), 1=Point(월드 pos 중심 반경 range 감쇠).
	void DrawLight2D(IRHICommandContext& commandContext, int type, float worldX, float worldY,
		float range, const float color[4], float intensity);
	// SceneColor × LightMap 을 현재 렌더패스에 풀스크린 컴포짓한다(곱셈, 최종 출력).
	void CompositeLighting(IRHICommandContext& commandContext, SafePtr<IRHITexture> sceneColor, SafePtr<IRHITexture> lightMap);

private:
	struct SpriteConstants
	{
		float TransformRow0[4];
		float TransformRow1[4];
		float Color[4];
		float ViewRow0[4];
		float ViewRow1[4];
		// UvRect(uMin,vMin,uScale,vScale) — cbuffer 마지막 필드(셰이더가 읽는 범위).
		// 뒤 SecondaryColor/ShaderParams 는 단일 스프라이트 셰이더가 읽지 않는 잔여 필드.
		float UvRect[4];
		float SecondaryColor[4];
		float ShaderParams[4];
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
		// UvRect(uMin,vMin,uScale,vScale) — per-instance 프레임 UV. 항등={0,0,1,1}.
		float UvRect[4];
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
	void ApplySurfacePreRotation(float (&viewRow0)[4], float (&viewRow1)[4]) const;
	SpriteInstanceData BuildSpriteInstanceData(const RenderItem& item) const;
	SpriteConstants BuildViewportColorConstants(float r, float g, float b, float a) const;
	SpriteConstants BuildLightConstants(int type, float worldX, float worldY, float range, const float color[4], float intensity, const ViewParameters& view) const;
	bool IsSpriteItemVisibleInView(const RenderItem& item, const ViewParameters& view) const;
	SafePtr<IRHIBuffer> AcquireSpriteConstantBuffer(IRHICommandContext& commandContext, const SpriteConstants& constants);
	SafePtr<IRHIBuffer> AcquireSpriteViewConstantBuffer(IRHICommandContext& commandContext, const SpriteViewConstants& constants);
	SafePtr<IRHIBuffer> AcquireSpriteInstanceBuffer(IRHICommandContext& commandContext, const SpriteInstanceData* instances, std::uint32_t instanceCount);
	bool DrawSpriteItem(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem& item, const SpriteDrawResources& resources, const ViewParameters& view);
	bool DrawSpriteQuad(IRHICommandContext& commandContext, RenderStateCache& stateCache, SafePtr<IRHIGraphicsPipeline> pipeline, SafePtr<IRenderMesh> mesh, SafePtr<IRHITexture> texture, SafePtr<IRHISampler> sampler, const SpriteConstants& constants);
	bool DrawSpriteBatch(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem* items, std::uint32_t itemCount, const SpriteDrawResources& resources, const ViewParameters& view);
	bool CanBatchSpriteItem(const RenderItem& item, const SpriteDrawResources& resources) const;
	bool CreateSpritePipeline();
	bool CreateTextPipeline();
	bool CreateSpriteBatchPipeline();
	// no-blend 풀스크린 blit 파이프라인(스프라이트 프로그램 재사용 + Opaque). BlitFullscreen 용.
	bool CreateBlitPipeline();
	// 라이트 누적 파이프라인(스프라이트 VS 재사용 + 라이트 PS + Additive). DrawLight2D 용.
	bool CreateLightPipeline();
	// SceneColor × LightMap 컴포짓 파이프라인(스프라이트 VS 재사용 + 컴포짓 PS + Opaque, 2텍스처).
	bool CreateCompositePipeline();
	bool CreateQuadMesh();

private:
	SafePtr<IRHIDevice> m_rhiDevice;
	OwnerPtr<IRHIProgram> m_spriteVertexProgram;
	OwnerPtr<IRHIProgram> m_spritePixelProgram;
	OwnerPtr<IRHIProgram> m_spriteBatchVertexProgram;
	OwnerPtr<IRHIProgram> m_spriteBatchPixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_spritePipeline;
	OwnerPtr<IRHIProgram> m_textVertexProgram;
	OwnerPtr<IRHIProgram> m_textPixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_textPipeline;
	OwnerPtr<IRHIGraphicsPipeline> m_spriteBatchPipeline;
	OwnerPtr<IRHIGraphicsPipeline> m_blitPipeline;
	OwnerPtr<IRHIProgram> m_lightPixelProgram;      // 라이트 감쇠 PS(스프라이트 VS 와 페어)
	OwnerPtr<IRHIGraphicsPipeline> m_lightPipeline;      // 라이트 누적(Additive)
	OwnerPtr<IRHIGraphicsPipeline> m_compositePipeline;  // 라이트맵 곱셈 컴포짓(sprite VS+PS, Multiply)
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
	// surface pre-rotation(클립공간) — 최종 뷰행렬에 곱해 표시 방향 보정. 기본 항등.
	float m_surfacePreRotCos = 1.0f;
	float m_surfacePreRotSin = 0.0f;
	RenderCullingStats m_lastCullingStats;
	// 1×1 white texture used by FillViewportColor
	OwnerPtr<IRHITexture> m_whiteTexture;

	// RenderWeave — transient RT 대여 풀(SceneColor/LightMap/Post 등 그래프 RT).
	RWTexturePool m_weavePool;
	bool m_isInitialized = false;
};
