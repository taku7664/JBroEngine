#pragma once

#include "Core/Platform/PlatformTypes.h"
#include "Core/Renderer/Render2DTypes.h"
#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Types/Color.h"

#include <span>

class IRHICommandContext;
class IRHIGpuTimer;
class IRHITexture;
class IRenderer;
class IRenderScene;

// ─────────────────────────────────────────────────────────────────────────────
//  Render2DPipeline — 2D 프레임 하나를 그리는 정책.
//
//  뷰포트 × 레이어를 순서대로 그리고, 라이팅·컴포짓·톤맵 포스트 체인을 RenderWeave 그래프로
//  엮는다. 입력은 전부 POD 스냅샷(Render2DTypes.h)이라 이 파일은 캔버스·카메라 컴포넌트·
//  레이어 객체를 전혀 모른다.
//
//  왜 Core 인가: 이 코드는 예전에 GameFramework(GameCamera.cpp)에 있었다. 그런데 하는 일이
//  "카메라 수집"이 아니라 렌더 패스 구성이라, GameFramework 를 뜯지 않고는 파이프라인을
//  바꿀 수 없었다(픽셀아트 전용·저사양 모드·포스트 이펙트 추가 전부). 수집(무엇을 그릴지)과
//  파이프라인(어떻게 그릴지)을 계층으로 가른 결과가 이 파일이다.
// ─────────────────────────────────────────────────────────────────────────────

// 이 프레임에 그릴 것과 어디에 그릴지.
struct Render2DFrameDesc
{
	// 뷰포트 목록. 비면 아무것도 그리지 않는다.
	std::span<const Render2DViewportDesc> Viewports;
	// 레이어 목록(아래→위). 비면 캔버스 전체를 한 레이어처럼 그린다(폴백).
	std::span<const Render2DLayerDesc>    Layers;
	// 월드 라이트. 비면 라이팅 패스 자체가 없다(패스스루 — 화면 불변).
	std::span<const Render2DLightDesc>    Lights;

	RenderSurfaceSize    TargetSize;
	SafePtr<IRHITexture> Target;
	// 캔버스 바탕색. null 이면 투명으로 클리어.
	const Color*         BackgroundColor = nullptr;
};

// 진단 훅 — 전부 옵션. 게임 런타임 경로는 기본값(전부 끔)으로 부른다.
struct Render2DDiagnostics
{
	// null 이 아니면 뷰포트별 컬링 통계를 채운다(호출 시 clear 후 append).
	std::vector<Render2DCameraStats>* OutCameraStats = nullptr;
	// null 이 아니면 레이어별 GPU 시간을 이 타이머로 잰다(게임뷰 렌더에서만 넘긴다).
	IRHIGpuTimer*                     GpuTimer = nullptr;
	// Active 면 드로우순서상 컷오프까지만 그린다(프로파일러 렌더타겟 진행 프리뷰).
	Render2DCutoff                    Cutoff;
	// 프로파일러 드로우순서 목록을 이 렌더에서 캡처할지. 게임뷰 렌더는 true, 프리뷰 렌더는
	// 게임뷰가 안 그려진 프레임에만 true(그래야 게임뷰 없이도 오브젝트 컷오프가 가능하다).
	// 컷오프가 걸려 있어도 캡처는 레이어 전체를 담는다(컷오프는 시각만 자른다).
	bool                              CaptureDrawOrder = false;
};

// 한 프레임을 그린다: 배경색 → for 뷰포트 { for 월드 레이어: 드로우 → 렉트에 컴포짓 }
//                    → 라이팅/컴포짓/톤맵 → 화면 공간 레이어(있으면).
void RenderScene2D(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const Render2DFrameDesc& frame,
	const Render2DDiagnostics& diagnostics = {});
