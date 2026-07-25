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
	CForward2DRenderer* AsForward2DRenderer() override { return this; }
	// 지정 키 집합에 속하는 RenderItem만 렌더링 (포커스 오버레이 / 마스크 패스용).
	void RenderFiltered(IRenderScene& scene, const std::unordered_set<RenderObjectId>& objects);
	// excluded 키 집합에 속하는 RenderItem을 제외하고 전부 렌더링 (에디터 씬뷰 숨김용).
	void RenderExcluding(IRenderScene& scene, const std::unordered_set<RenderObjectId>& excluded);
	// 한 레이어의 아이템만 렌더링한다. 정렬 1순위가 LayerIndex 라 레이어 구간이 연속이므로
	// 전체 순회 후 필터링이 아니라 구간만 훑는다(레이어 수 × 아이템 수 비용 회피).
	// excluded = 에디터 숨김 키 집합(없으면 nullptr).
	// maxDrawCount = 레이어 드로우순서에서 앞에서부터 이 개수까지만 그린다(기본 = 전체). 프로파일러
	// 렌더타겟 진행 프리뷰가 "이 오브젝트까지" 컷오프를 걸 때 쓴다.
	void RenderLayer(
		IRenderScene& scene,
		RenderLayerIndex layerIndex,
		const std::unordered_set<RenderObjectId>* excluded = nullptr,
		std::uint32_t maxDrawCount = 0xFFFFFFFFu);
	void Finalize() override;

	bool CreateGpuResource(IRenderResource& resource) override;
	void DestroyGpuResource(IRenderResource& resource) override;

	SafePtr<IRHIGraphicsPipeline> GetSpritePipeline() const;
	SafePtr<IRHIGraphicsPipeline> GetTextPipeline() const;
	SafePtr<IRHISampler> GetDefaultSampler() const;
	SafePtr<IRenderMesh> GetQuadMesh() const;
	SafePtr<IRHITexture> GetWhiteTexture() const;

	// ── 진단(에디터 GPU 프로파일러 드로우순서/배칭 표시) ─────────────────────────
	// 이 아이템이 인스턴싱 배치 대상인지와, 같은 배치로 묶이려면 일치해야 하는 텍스처/샘플러
	// 식별자(불투명 포인터, 비교 전용). RenderWithSkip 의 배치 판정과 동일 근거를 쓴다
	// (ResolveSpriteDrawResources + CanBatchSpriteItem). 뷰 무관 — 연속 런을 묶는 그룹핑
	// 루프는 호출자(GameCamera 드로우순서 캡처)가 돌린다. 계측 on 일 때만 호출되는 진단 경로.
	struct RenderItemBatchKey
	{
		bool        Batchable = false;
		const void* Texture   = nullptr;
		const void* Sampler   = nullptr;
	};
	RenderItemBatchKey GetItemBatchKey(const RenderItem& item) const;

	// ── RenderWeave: 렌더그래프 RT 풀 + 풀스크린 blit ────────────────────────
	// transient RT 대여 풀. RenderGameCameraStack 이 프레임마다 RWGraph 를 구성해 쓴다.
	RWTexturePool& GetRenderWeavePool();
	// src 텍스처를 현재 바인딩된 렌더패스에 풀스크린으로 복사한다(전용 no-blend 파이프라인).
	void BlitFullscreen(IRHICommandContext& commandContext, SafePtr<IRHITexture> src);
	// 레이어 RT(src)를 현재 렌더패스에 blendMode 로 컴포짓한다. src 는 투명 RT 에 AlphaBlend 로
	// 그려져 premultiplied 상태이므로 Layer* 블렌드가 알파를 재적용하지 않는다. opacity 는
	// premultiplied 색·알파에 스칼라 곱(색 틴트)이면 되므로 별도 셰이더가 필요 없다.
	void CompositeLayer(
		IRHICommandContext& commandContext,
		SafePtr<IRHITexture> src,
		ERHIBlendMode blendMode,
		float opacity);

	// ── RenderWeave 라이팅 ──────────────────────────────────────────────────────
	// 라이트 1개를 LightMap 에 가산 누적한다(현재 SetViewCameraEx 로 설정된 뷰 기준).
	// type: 0=Directional(뷰포트 균일), 1=Point(월드 pos 중심 반경 range 감쇠).
	// shadowAtlas(폴라 1D 섀도맵 256×N) + shadowRow(이 라이트의 행) + shadowRowCount + softness01 를 주면
	// 픽셀 (θ,r)로 아틀라스를 샘플해 그림자를 적용한다. shadowRow<0(기본) = 무그림자.
	void DrawLight2D(IRHICommandContext& commandContext, int type, float worldX, float worldY,
		float range, const float color[4], float intensity,
		SafePtr<IRHITexture> shadowAtlas = {}, int shadowRow = -1, int shadowRowCount = 1, float softness01 = 0.0f);
	// Spot 원뿔광 — Point 반경 감쇠 × 각도 감쇠(inner~outer). dir = 월드 방향(정규화).
	void DrawLight2DSpot(IRHICommandContext& commandContext, float worldX, float worldY,
		float range, const float color[4], float intensity,
		float dirX, float dirY, float innerAngleRadians, float outerAngleRadians,
		SafePtr<IRHITexture> shadowAtlas = {}, int shadowRow = -1, int shadowRowCount = 1, float softness01 = 0.0f);
	// CastShadow 스프라이트의 알파 실루엣을 현재 바인딩된 OccluderMap 타겟에 그린다(월드공간 공유 오클루더 맵).
	// 뷰는 호출자가 SetViewCameraEx 로 설정. Additive(불투명=1). 광원별 제외 없음(공유 맵).
	void RenderOccluders(IRHICommandContext& commandContext, IRenderScene& scene);
	// 폴라 1D 섀도맵 리덕션 — OccluderMap 을 광원 중심 각도별 최근접 오클루더 거리로 축약해 현재 바인딩된
	// ShadowAtlas 행(호출자가 viewport 로 지정)에 그린다. cam* = OccluderMap 을 그린 카메라 뷰(월드→NDC 매핑).
	void DrawPolarReduction(IRHICommandContext& commandContext, SafePtr<IRHITexture> occluderMap,
		float camX, float camY, float camHalfW, float camHalfH, float camCosR, float camSinR,
		float lightX, float lightY, float lightRange);
	// 라이팅/컴포짓 파이프라인이 런타임 생성(셰이더 컴파일)됐는지 — 진단용.
	bool AreLightingPipelinesReady() const;
	// SceneColor × LightMap 을 현재 렌더패스에 풀스크린 컴포짓한다(곱셈, HDR 결과).
	void CompositeLighting(IRHICommandContext& commandContext, SafePtr<IRHITexture> sceneColor, SafePtr<IRHITexture> lightMap);
	// HDR 텍스처(src)를 Reinhard 톤맵으로 현재 렌더패스에 풀스크린 출력한다(밝기 롤오프).
	void TonemapFullscreen(IRHICommandContext& commandContext, SafePtr<IRHITexture> src);

private:
	struct SpriteConstants
	{
		float TransformRow0[4];
		float TransformRow1[4];
		float Color[4];
		float ViewRow0[4];
		float ViewRow1[4];
		// UvRect(uMin,vMin,uScale,vScale) — cbuffer 마지막 필드(셰이더가 읽는 범위).
		// 뒤 SecondaryColor/ShaderParams/ShadowParams 는 단일 스프라이트 셰이더가 읽지 않는 잔여 필드.
		float UvRect[4];
		float SecondaryColor[4];
		float ShaderParams[4];
		// 폴라 섀도맵 라이트 전용: x=행 인덱스(-1=무그림자), y=행 수, z=softness01(페넘브라 폭), w=예비.
		float ShadowParams[4];
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
	// RenderImpl/RenderFiltered 의 공통 순회·배칭 본체. shouldSkip(entity) 가 true 인 아이템만
	// 건너뛴다(정의는 .cpp — 두 래퍼가 같은 TU 에서 인스턴스화). 핫루프라 std::function 대신 템플릿.
	// range = 그릴 아이템 구간(레이어 단위 드로우용). 전체를 그리려면 {0, itemCount}.
	template<typename FSkip>
	void RenderWithSkip(IRenderScene& scene, const RenderItemRange& range, FSkip&& shouldSkip);
	ViewParameters BuildViewParameters() const;
	SpriteDrawResources ResolveSpriteDrawResources(const RenderItem& item) const;
	SpriteConstants BuildSpriteConstants(const RenderItem& item, const ViewParameters& view) const;
	SpriteViewConstants BuildSpriteViewConstants(const ViewParameters& view) const;
	void ApplySurfacePreRotation(float (&viewRow0)[4], float (&viewRow1)[4]) const;
	SpriteInstanceData BuildSpriteInstanceData(const RenderItem& item) const;
	SpriteConstants BuildViewportColorConstants(float r, float g, float b, float a) const;
	SpriteConstants BuildLightConstants(int type, float worldX, float worldY, float range, const float color[4], float intensity, const ViewParameters& view) const;
	// DrawLight2D/DrawLight2DSpot 공통 꼬리 — 그림자 파라미터를 채우고 라이트 쿼드를 그린다.
	void SubmitLightQuad(IRHICommandContext& commandContext, SpriteConstants& constants,
		SafePtr<IRHITexture> shadowAtlas, int shadowRow, int shadowRowCount, float softness01);
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
	// 레이어 컴포짓 파이프라인 4종(스프라이트 프로그램 재사용 + Layer* 블렌드). CompositeLayer 용.
	bool CreateLayerCompositePipelines();
	// 라이트 누적 파이프라인(스프라이트 VS 재사용 + 라이트 PS + Additive). DrawLight2D 용.
	bool CreateLightPipeline();
	// OccluderMap 채움 파이프라인(스프라이트 VS + 알파→흰색 PS + Additive). RenderOccluders 용.
	bool CreateOccluderFillPipeline();
	// 폴라 리덕션 파이프라인(전용 풀스크린 VS + 레이마치 PS + Opaque). DrawPolarReduction 용.
	bool CreatePolarReductionPipeline();
	// SceneColor × LightMap 컴포짓 파이프라인(스프라이트 VS 재사용 + 컴포짓 PS + Opaque, 2텍스처).
	bool CreateCompositePipeline();
	// Reinhard 톤맵 파이프라인(스프라이트 VS + 톤맵 PS + Opaque). TonemapFullscreen 용.
	bool CreateTonemapPipeline();
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
	// 레이어 컴포짓 파이프라인 — 인덱스 = ELayerCompositeBlend(Normal/Additive/Multiply/Screen).
	// 배열 = 블렌드 모드로 O(1) 선택(프레임 루프에서 분기·조회 비용 없음).
	OwnerPtr<IRHIGraphicsPipeline> m_layerCompositePipelines[4];
	OwnerPtr<IRHIProgram> m_lightPixelProgram;      // 라이트 감쇠 PS(스프라이트 VS 와 페어)
	OwnerPtr<IRHIGraphicsPipeline> m_lightPipeline;      // 라이트 누적(Additive)
	OwnerPtr<IRHIProgram> m_occluderFillPixelProgram;        // occluder 알파→흰색 PS
	OwnerPtr<IRHIGraphicsPipeline> m_occluderFillPipeline;   // OccluderMap 채움(Additive)
	OwnerPtr<IRHIProgram> m_polarReductionVertexProgram;     // 리덕션 풀스크린 VS
	OwnerPtr<IRHIProgram> m_polarReductionPixelProgram;      // 리덕션 레이마치 PS
	OwnerPtr<IRHIGraphicsPipeline> m_polarReductionPipeline; // 폴라 1D 리덕션(Opaque)
	OwnerPtr<IRHIGraphicsPipeline> m_compositePipeline;  // 라이트맵 곱셈 컴포짓(sprite VS+PS, Multiply)
	OwnerPtr<IRHIProgram> m_tonemapPixelProgram;         // Reinhard 톤맵 PS
	OwnerPtr<IRHIGraphicsPipeline> m_tonemapPipeline;    // HDR→LDR 톤맵 blit(sprite VS + 톤맵 PS, Opaque)
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
