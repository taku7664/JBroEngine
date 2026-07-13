#pragma once

#include "Utillity/Pointer/SafePtr.h"

#include <unordered_set>

class IRHIDevice;
class IRHICommandContext;
class IRHITexture;
class IRHIProgram;
class IRHIGraphicsPipeline;
class IRHISampler;
class IRHIBuffer;
class IRenderScene;
class CForward2DRenderer;

// ── COutlineRenderer2D ────────────────────────────────────────────────────────
//
// GPU Separable Alpha-Dilation 방식의 2D 스프라이트 아웃라인 렌더러.
//
// 사용 순서:
//   1. RenderMask(...)   — 마스크 렌더 + 수평/수직 분리 팽창 패스 실행
//                          (모두 BeginRenderPass 밖에서 실행)
//   2. RenderOutline(...) — 현재 바인딩된 RT에 아웃라인 덧그리기
//                           (RenderPass 안에서 호출)
//
// Separable Alpha Dilation:
//   Pass 1 (H-Dilate): maskRT → hDilatedRT  — 수평 방향 max-alpha 팽창
//   Pass 2 (V-Dilate): hDilatedRT → vDilatedRT — 수직 방향 max-alpha 팽창
//   Pass 3 (Composite): maskRT + vDilatedRT → 씬 RT
//                        vDilated.a > 0.5 && mask.a < 0.5 → 아웃라인 픽셀
//
// 성능:
//   구버전 단일패스 O(r²) 샘플 → 신버전 2×O(r) 샘플
//   r=2 기준: 25샘플 → 10샘플 (5H + 5V)

class COutlineRenderer2D
{
public:
	bool Initialize(SafePtr<IRHIDevice> rhiDevice);
	void Finalize();

	// Step 1: 마스크 렌더 + H/V 팽창 패스 (BeginRenderPass 밖에서 호출).
	//   outlineWidthPx: 팽창 커널 반경 (픽셀). RenderOutline 호출값과 일치시킬 것.
	void RenderMask(
		SafePtr<IRHICommandContext> ctx,
		CForward2DRenderer& spriteRenderer,
		IRenderScene& scene,
		const std::unordered_set<DebugObjectId>& entities,
		float camX, float camY, float camSize,
		int viewW, int viewH,
		float outlineWidthPx = 2.0f);

	// Step 2: 현재 RenderPass 안에서 아웃라인을 합성.
	//   r/g/b/a: 아웃라인 색상 (0~1)
	//   outlineWidthPx: 기록용 (이미 RenderMask에서 팽창 완료됨, CB 갱신에만 사용)
	void RenderOutline(
		SafePtr<IRHICommandContext> ctx,
		float r, float g, float b, float a,
		float outlineWidthPx,
		int viewW, int viewH);

private:
	bool CreatePipelines();
	void EnsureRTs(uint32_t w, uint32_t h);  // maskRT + hDilatedRT + vDilatedRT 생성/갱신

	SafePtr<IRHIDevice>            m_rhiDevice;

	// ── Render Targets ────────────────────────────────────────────────────────
	OwnerPtr<IRHITexture>          m_maskRT;       // 스프라이트 마스크
	OwnerPtr<IRHITexture>          m_hDilatedRT;   // 수평 팽창 결과
	OwnerPtr<IRHITexture>          m_vDilatedRT;   // 수직 팽창 결과 (최종 dilated mask)
	uint32_t m_maskWidth  = 0;
	uint32_t m_maskHeight = 0;

	// ── Shaders ───────────────────────────────────────────────────────────────
	OwnerPtr<IRHIProgram>          m_vsProgram;        // 공유 fullscreen VS
	OwnerPtr<IRHIProgram>          m_hDilationPS;      // PSHDilate
	OwnerPtr<IRHIProgram>          m_vDilationPS;      // PSVDilate
	OwnerPtr<IRHIProgram>          m_compositePS;      // PSComposite

	// ── Pipelines ─────────────────────────────────────────────────────────────
	OwnerPtr<IRHIGraphicsPipeline> m_hDilationPipeline;   // Opaque
	OwnerPtr<IRHIGraphicsPipeline> m_vDilationPipeline;   // Opaque
	OwnerPtr<IRHIGraphicsPipeline> m_compositePipeline;   // AlphaBlend

	// ── Resources ─────────────────────────────────────────────────────────────
	OwnerPtr<IRHIBuffer>           m_constantBuffer;   // 프레임 간 재사용 CB
	OwnerPtr<IRHISampler>          m_pointSampler;

	bool m_isInitialized = false;
};
