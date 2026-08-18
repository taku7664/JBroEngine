#include "pch.h"
#include "Render2DPipeline.h"

#include "Core/EngineCore.h"                    // Engine.GpuProfiler — 드로우순서 캡처(에디터 진단)
#include "Core/Debug/GpuProfiler.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIGpuTimer.h"              // 레이어별 GPU 시간 계측(에디터 진단)
#include "Core/Renderer/Forward2DRenderer.h"   // CForward2DRenderer — 그래프/컴포짓 경로
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/IRenderer.h"
#include "Core/Renderer/RenderCulling.h"       // IsRenderItemVisible — 렌더러와 공용 컬링 판정
#include "Core/RuntimeConfig.h"                 // 전역 앰비언트 / 화면 공간 기준 해상도

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
	// 레이어 뷰 = 뷰포트 뷰를 레이어의 스페이스로 해석한 것.
	//
	//  · Screen : 카메라를 아예 안 본다 — 원점 고정 · 무회전 · 스케일 모드가 정하는 오쏘 범위.
	//             그래서 카메라가 움직이든 줌하든 회전하든 화면상 위치가 불변이다.
	//  · World  : ParallaxFactor 로 카메라 **위치만** 배율(1=동일, 0.5=절반 속도 원경,
	//             0=월드 원점 고정). 회전·줌은 카메라를 그대로 따른다 — 그래서 factor 0 은
	//             "화면 고정"이 아니다(줌하면 커지고 회전하면 기운다). 화면 고정은 Screen 이다.
	//
	// rtW/rtH = 렌더 타깃 픽셀 크기. 뷰포트 렉트가 정규화라 픽셀로 되돌려야 종횡비가 나온다
	// (화면 전체가 아니라 이 뷰포트 렉트 기준 — 스플릿에서 내용이 찌그러지지 않게).
	//
	// 뷰포트 desc 가 아니라 **뷰만** 돌려준다 — desc 를 복사하면 레이어 필터 vector 까지 복사되어
	// (뷰포트 × 레이어)마다 프레임 힙 할당이 붙는다.
	Render2DView ApplyLayerSpace(
		const Render2DViewportDesc& viewport,
		const Render2DLayerDesc& layer,
		const ScreenSpaceReference& screenReference,
		float rtW,
		float rtH)
	{
		if (layer.ScreenSpace)
		{
			Render2DView view = viewport.View;
			view.PosX = 0.0f;
			view.PosY = 0.0f;
			view.CosR = 1.0f;
			view.SinR = 0.0f;
			ComputeScreenSpaceExtents(layer.ScaleMode, screenReference,
				viewport.RectW * rtW, viewport.RectH * rtH,
				view.OrthoSizeX, view.OrthoSize);
			return view;
		}

		if (1.0f == layer.ParallaxFactor)
		{
			return viewport.View;
		}
		Render2DView view = viewport.View;
		view.PosX = viewport.View.PosX * layer.ParallaxFactor;
		view.PosY = viewport.View.PosY * layer.ParallaxFactor;
		return view;
	}

	// 프로파일러 드로우순서 캡처가 오브젝트별 컬링 여부를 표시하려고 쓰는 판정.
	// 판정 본체는 렌더러와 **같은 공용 함수**다(RenderCulling.h) — 예전엔 4코너 루프를 여기에
	// 복제해 뒀어서, 컬링 규칙을 바꾸면 에러 없이 프로파일러 표시만 어긋날 수 있었다.
	// GameView 는 excluded 없이 프러스텀 컬링만 적용하므로 이 판정이 실제 드로우 여부와 일치한다.
	bool ItemVisibleInView(const RenderItem& item, const Render2DView& view)
	{
		return IsRenderItemVisible(item, view.PosX, view.PosY, view.OrthoSizeX, view.OrthoSize, view.CosR, view.SinR);
	}

	// 한 레이어를 현재 바인딩된 타겟(뷰포트 설정 완료 상태)에 그린다.
	// drawAllItems = 레이어 구간이 아니라 캔버스 전체를 그린다(레이어 스냅샷이 없는 폴백 경로).
	void DrawLayerItems(
		IRenderer& renderer,
		CForward2DRenderer* forward,
		IRenderScene& renderScene,
		const Render2DView& view,
		const Render2DLayerDesc& layer,
		bool drawAllItems,
		std::uint32_t maxDrawCount = 0xFFFFFFFFu)
	{
		renderer.SetViewCameraEx(view.PosX, view.PosY, view.OrthoSizeX, view.OrthoSize, view.CosR, view.SinR);
		if (forward && false == drawAllItems)
		{
			// maxDrawCount = 프로파일러 컷오프 프리뷰에서 이 레이어의 드로우순서 앞부분만 그릴 때 쓴다.
			forward->RenderLayer(renderScene, layer.Index, nullptr, maxDrawCount);
		}
		else
		{
			// Forward2D 가 아니거나 레이어 스냅샷이 없으면 레이어 분할 없이 전체를 그린다.
			renderer.Render(renderScene);
		}
	}

	// 뷰포트 목록을 지정 타겟에 그린다. 그래프 Base 패스와 폴백 경로가 공유한다.
	// 바탕색을 한 번 깔고, 뷰포트마다 그 뷰포트가 맡은 레이어를 캔버스 순서(아래→위)로
	// 그린 뒤 즉시 합성한다:
	//   · 평범한 레이어(Normal+불투명) → 타겟에 직접 드로우(RT 왕복 없음)
	//   · 그 외                                → 스크래치 RT 에 그린 뒤 블렌드·Opacity 로 합성
	// 같은 레이어가 뷰포트마다 각자의 눈으로 다시 그려지는 것이 멀티뷰포트의 본체다.
	// outCameraStats 에 뷰포트별 통계를 append(호출자가 미리 clear).
	void RenderViewportsInto(
		IRHICommandContext& commandContext,
		IRenderer& renderer,
		IRenderScene& renderScene,
		std::span<const Render2DViewportDesc> viewports,
		std::span<const Render2DLayerDesc> layers,
		const RenderSurfaceSize& renderTargetSize,
		SafePtr<IRHITexture> target,
		std::vector<Render2DCameraStats>* outCameraStats,
		const Color* backgroundColor,
		IRHIGpuTimer* gpuTimer,
		Render2DCutoff cutoff,
		bool captureDrawOrder,
		// 이 호출이 그릴 스페이스. 월드 패스와 화면 오버레이 패스가 같은 함수를 두 번 타되
		// 서로의 레이어를 건너뛴다. 목록을 둘로 쪼개 넘기지 않는 이유는 매 프레임 도는
		// 경로라 vector 두 개가 곧 프레임당 힙 할당이기 때문이다.
		bool screenSpacePass = false,
		// 오버레이 호출은 이미 그려진 화면 위에 얹혀야 하므로 클리어하면 안 된다.
		bool clearTarget = true)
	{
		const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
		const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

		// 화면 공간 저작 기준 — 호스트 전용 코드라 Runtime 을 직접 읽어도 된다
		// (공용 계산 함수 쪽은 DLL 에도 링크되므로 인자로 받는다).
		const ScreenSpaceReference screenReference = ScreenSpaceReference::FromResolution(
			Runtime.ReferenceResolutionWidth, Runtime.ReferenceResolutionHeight, Runtime.PixelsPerUnit);

		// 컴포짓 바탕 = 캔버스 배경색(없으면 투명). 레이어 RT 는 투명으로 클리어되고
		// 이 위에 순서대로 얹힌다 — 구 카메라별 ClearColor 를 캔버스급으로 승계한 것.
		if (clearTarget)
		{
			RenderPassDesc clearDesc;
			clearDesc.ColorAttachment.Target = target;
			clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			if (nullptr != backgroundColor)
			{
				clearDesc.ColorAttachment.SetClearColor(backgroundColor->Data());
			}
			else
			{
				clearDesc.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			}
			commandContext.BeginRenderPass(clearDesc);
			commandContext.EndRenderPass();
		}

		CForward2DRenderer* forward = renderer.AsForward2DRenderer();

		// 레이어 스냅샷이 없으면(레이어를 안 넘긴 호출자) 캔버스 전체를 한 레이어처럼 그린다 —
		// 조용히 레이어 0 만 그리는 사고를 막는 폴백.
		const bool drawAllItems = layers.empty();
		const std::vector<Render2DLayerDesc> implicitLayers(drawAllItems ? 1 : 0);
		const std::span<const Render2DLayerDesc> effectiveLayers =
			drawAllItems ? std::span<const Render2DLayerDesc>(implicitLayers) : layers;

		for (const Render2DViewportDesc& viewport : viewports)
		{
			const float vpX = viewport.RectX * rtW;
			const float vpY = viewport.RectY * rtH;
			const float vpW = std::max(viewport.RectW * rtW, 1.0f);
			const float vpH = std::max(viewport.RectH * rtH, 1.0f);
			const RenderSurfaceSize viewportSize{ static_cast<int>(vpW), static_cast<int>(vpH) };

			RenderPassDesc renderPassDesc;
			renderPassDesc.ColorAttachment.Target = target;
			renderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Load;
			renderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;

			RenderCullingStats cameraStats;
			for (const Render2DLayerDesc& layer : effectiveLayers)
			{
				if (false == layer.Visible)
				{
					continue;
				}
				// 이 호출이 맡은 스페이스만. 나머지는 짝이 되는 다른 호출이 그린다.
				if (layer.ScreenSpace != screenSpacePass)
				{
					continue;
				}
				// 레이어 필터 — 비면 전체 레이어(대부분). 목록이 작아 선형 탐색으로 충분하다.
				if (false == viewport.Layers.empty()
					&& std::find(viewport.Layers.begin(), viewport.Layers.end(), layer.Index) == viewport.Layers.end())
				{
					continue;
				}
				// 컷오프 레이어면 드로우순서상 ObjectDrawIndex(포함)까지만. UINT32_MAX 면 레이어 전체.
				std::uint32_t layerMaxDraw = 0xFFFFFFFFu;
				if (cutoff.Active && layer.Index == cutoff.LayerIndex && cutoff.ObjectDrawIndex != 0xFFFFFFFFu)
				{
					layerMaxDraw = cutoff.ObjectDrawIndex + 1u;
				}
				// 아이템 없는 레이어는 통째로 건너뛴다 — 특히 RT 경유 레이어에서 빈 스크래치를
				// 빌려 투명 합성하는 낭비를 없앤다(에디터는 전 레이어 RT 강제라 더 크게 절약).
				// Sort 는 프레임당 1회만 실제 작업을 하므로 구간 조회는 저렴하다.
				if (false == drawAllItems)
				{
					renderScene.Sort();
					if (0 == renderScene.GetLayerRange(layer.Index).Count)
					{
						continue;
					}
				}

				const Render2DView view = ApplyLayerSpace(viewport, layer, screenReference, rtW, rtH);

				// 드로우순서 오브젝트 캡처(captureDrawOrder + 계측 on). 씬이 살아있는 지금 담아 두면,
				// 창이 같은 프레임에 오브젝트 포인터를 이름으로 역해석한다. GetLayerRange 는 위 컬링
				// 검사에서 이미 Sort 를 거쳤다. 컬링 여부는 이 레이어 뷰로 재판정한다.
				// 컷오프 '위' 레이어 skip 보다 먼저 돌기 때문에, 컷오프 프리뷰(게임뷰 꺼짐)에서도
				// 전체 드로우순서가 채워진다 — 컷오프는 아래 시각(드로우)만 자르고 캡처는 레이어 전체를 담는다.
				if (captureDrawOrder && Engine.GpuProfiler.IsValid() && Engine.GpuProfiler->IsEnabled())
				{
					const RenderItemRange drawRange = renderScene.GetLayerRange(layer.Index);
					const RenderItem* drawItems = renderScene.GetRenderItems();
					CGpuProfiler* profiler = Engine.GpuProfiler.TryGet();
					profiler->BeginLayerDrawOrder(layer.Index);

					// 배치 그룹핑을 RenderWithSkip 와 같은 규칙으로 재현한다 — 가시·같은 텍스처/샘플러·
					// 배치가능 아이템의 최대 런이 하나의 인스턴싱 드로우콜(같은 group).
					// **컬링된 아이템은 런을 끊지 않는다** — 그려지지 않으므로 렌더러도 건너뛰고 런을 잇는다.
					// 배치불가 아이템만 런을 끊는다. 배치 판정 근거는 forward->GetItemBatchKey 가 유일
					// 출처라 핫패스와 안 어긋난다.
					// (게임뷰는 excluded 없이 컬링만 적용하므로 shouldSkip 재현 없이 실제 드로우와 일치한다.)
					std::uint32_t nextGroup = 0;
					std::uint32_t runGroup = 0;
					bool runOpen = false;
					const void* runTexture = nullptr;
					const void* runSampler = nullptr;
					for (std::uint32_t di = 0; di < drawRange.Count; ++di)
					{
						const RenderItem& drawItem = drawItems[drawRange.Begin + di];
						if (false == ItemVisibleInView(drawItem, view))
						{
							profiler->RecordDrawOrderItem(drawItem.Entity, true, INVALID_DRAW_GROUP);
							continue;   // 런은 유지 — 렌더러도 이 아이템을 건너뛰고 런을 이어 간다.
						}

						const CForward2DRenderer::RenderItemBatchKey key =
							forward ? forward->GetItemBatchKey(drawItem) : CForward2DRenderer::RenderItemBatchKey{};
						std::uint32_t group;
						if (key.Batchable && runOpen && key.Texture == runTexture && key.Sampler == runSampler)
						{
							group = runGroup;   // 같은 배치 런 연장.
						}
						else if (key.Batchable)
						{
							group = runGroup = nextGroup++;   // 새 배치 런 시작.
							runOpen = true;
							runTexture = key.Texture;
							runSampler = key.Sampler;
						}
						else
						{
							group = nextGroup++;   // 배치 불가 — 독립 드로우, 런 종료.
							runOpen = false;
						}
						profiler->RecordDrawOrderItem(drawItem.Entity, false, group);
					}
				}

				// 프로파일러 컷오프 프리뷰 — 선택 지점보다 위(인덱스 큰) 레이어는 시각적으로만 안 그린다
				// (드로우순서 목록 캡처는 위에서 이미 끝냈다). 캡처 뒤에 두어야 게임뷰 없이도 목록이 찬다.
				if (cutoff.Active && layer.Index > cutoff.LayerIndex)
				{
					continue;
				}

				// 레이어 GPU 구간 시작(게임뷰에서만 gpuTimer != null). 컬링/필터/컷오프로 건너뛴 레이어는
				// 여기 도달 전에 continue 되므로, 실제로 그리는 레이어만 계측된다.
				const std::uint32_t gpuScope =
					gpuTimer ? gpuTimer->BeginScope(GpuLayerKey(layer.Index)) : INVALID_GPU_SCOPE;

				if (false == layer.NeedsOwnTexture || nullptr == forward)
				{
					// 직접 경로 — 레이어 도입 전과 동일한 드로우 비용.
					commandContext.BeginRenderPass(renderPassDesc);
					commandContext.SetViewport(vpX, vpY, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					DrawLayerItems(renderer, forward, renderScene, view, layer, drawAllItems, layerMaxDraw);
					commandContext.EndRenderPass();
				}
				else
				{
					// RT 경유 — 뷰포트 크기 스크래치를 빌려 투명 클리어 후 그리고, 그 결과를
					// 뷰포트 렉트에 컴포짓한다. 스프라이트가 AlphaBlend 로 투명 RT 에 그려지면
					// premultiplied 색이 남으므로 Layer* 블렌드가 알파를 재적용하지 않는다.
					RWTextureDesc scratchDesc;
					scratchDesc.Width = static_cast<std::uint32_t>(std::max(1, viewportSize.Width));
					scratchDesc.Height = static_cast<std::uint32_t>(std::max(1, viewportSize.Height));
					scratchDesc.Format = ERHITextureFormat::RGBA8;
					SafePtr<IRHITexture> scratch = forward->GetRenderWeavePool().Acquire(scratchDesc);
					if (false == scratch.IsValid())
					{
						continue;
					}

					RenderPassDesc layerClear;
					layerClear.ColorAttachment.Target = scratch;
					layerClear.ColorAttachment.LoadOp = ERHILoadOp::Clear;
					layerClear.ColorAttachment.StoreOp = ERHIStoreOp::Store;
					layerClear.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
					commandContext.BeginRenderPass(layerClear);
					commandContext.EndRenderPass();

					RenderPassDesc layerPass;
					layerPass.ColorAttachment.Target = scratch;
					layerPass.ColorAttachment.LoadOp = ERHILoadOp::Load;
					layerPass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
					commandContext.BeginRenderPass(layerPass);
					commandContext.SetViewport(0.0f, 0.0f, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					DrawLayerItems(renderer, forward, renderScene, view, layer, drawAllItems, layerMaxDraw);
					commandContext.EndRenderPass();

					commandContext.BeginRenderPass(renderPassDesc);
					commandContext.SetViewport(vpX, vpY, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					forward->CompositeLayer(commandContext, scratch, layer.BlendMode, layer.Opacity);
					commandContext.EndRenderPass();

					// 합성이 끝나면 이 스크래치는 아무도 읽지 않는다 — 즉시 반납해 다음 레이어가
					// 같은 RT 를 재사용한다. 반납이 없으면 레이어 수 × 뷰포트 수만큼의 뷰포트
					// 크기 RT 가 프레임 내내 동시 상주한다(에디터는 전 레이어 RT 강제라 더 크다).
					forward->GetRenderWeavePool().Release(scratch);
				}

				if (gpuTimer)
				{
					gpuTimer->EndScope(gpuScope);
				}

				const RenderCullingStats layerStats = renderer.GetLastCullingStats();
				cameraStats.SubmittedCount += layerStats.SubmittedCount;
				cameraStats.DrawnCount += layerStats.DrawnCount;
				cameraStats.CulledCount += layerStats.CulledCount;
			}

			if (outCameraStats)
			{
				Render2DCameraStats stats;
				stats.OwnerObject = viewport.CameraOwnerObject;
				stats.Culling = cameraStats;
				outCameraStats->push_back(stats);
			}
		}
	}
}

void RenderScene2D(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const Render2DFrameDesc& frame,
	const Render2DDiagnostics& diagnostics)
{
	std::vector<Render2DCameraStats>* outCameraStats = diagnostics.OutCameraStats;
	if (outCameraStats)
	{
		outCameraStats->clear();
	}

	if (frame.Viewports.empty())
	{
		return;
	}

	const std::span<const Render2DViewportDesc> viewports = frame.Viewports;
	const std::span<const Render2DLayerDesc> layers = frame.Layers;
	const std::span<const Render2DLightDesc> lights = frame.Lights;
	const RenderSurfaceSize renderTargetSize = frame.TargetSize;
	const Color* backgroundColor = frame.BackgroundColor;
	IRHIGpuTimer* gpuTimer = diagnostics.GpuTimer;
	const Render2DCutoff cutoff = diagnostics.Cutoff;
	const bool captureDrawOrder = diagnostics.CaptureDrawOrder;

	const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
	const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

	// 화면 공간 레이어가 하나라도 있으면 라이팅 뒤에 따로 그려야 한다(아래 Overlay).
	// 스캔만 하고 목록을 만들지 않는다 — 매 프레임 도는 경로라 vector 가 곧 힙 할당이다.
	bool hasScreenLayers = false;
	for (const Render2DLayerDesc& layer : layers)
	{
		if (layer.Visible && layer.ScreenSpace)
		{
			hasScreenLayers = true;
			break;
		}
	}

	// Forward2DRenderer 가 아니면 렌더그래프/RT풀이 없으므로 최종 타겟에 직접 렌더(폴백).
	CForward2DRenderer* forward = renderer.AsForward2DRenderer();
	if (nullptr == forward)
	{
		RenderViewportsInto(commandContext, renderer, renderScene, viewports, layers, renderTargetSize, frame.Target, outCameraStats, backgroundColor, gpuTimer, cutoff, captureDrawOrder, false, true);
		if (hasScreenLayers)
		{
			// 폴백엔 라이팅 자체가 없지만 순서는 같게 — 화면 레이어는 항상 월드 위다.
			RenderViewportsInto(commandContext, renderer, renderScene, viewports, layers, renderTargetSize, frame.Target, nullptr, nullptr, gpuTimer, cutoff, captureDrawOrder, true, false);
		}
		renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
		return;
	}

	// ── RenderWeave 그래프 ──────────────────────────────────────────────────────
	// 항상: Base(뷰포트 × **월드** 레이어 → SceneColor).
	// 라이팅 비활성(라이트 0 + 앰비언트 백색): Blit(SceneColor → 월드결과). 화면 불변.
	// 라이팅 활성: LightMap(앰비언트 클리어 + Light2D 가산) → Composite(SceneColor × LightMap) → Tonemap.
	// 화면 레이어가 있으면 마지막에: Overlay(월드결과 → 최종, 그 위에 화면 레이어).
	// 패스를 데이터로 선언하면 그래프가 RT 대여/컬링/실행을 담당한다.
	const float* ambient = Runtime.AmbientLight;
	// 라이트가 있을 때만 라이팅 경로. 라이트0 캔버스 = 패스스루(앰비언트 무관, 화면 불변).
	const bool lightingActive = (false == lights.empty());

	RWGraph graph(forward->GetRenderWeavePool());
	const RWTextureHandle hFinal = graph.ImportTexture(frame.Target);

	RWTextureDesc sceneDesc;
	sceneDesc.Width  = static_cast<std::uint32_t>(std::max(1, renderTargetSize.Width));
	sceneDesc.Height = static_cast<std::uint32_t>(std::max(1, renderTargetSize.Height));
	sceneDesc.Format = ERHITextureFormat::RGBA8;
	const RWTextureHandle hScene = graph.CreateTexture(sceneDesc);

	// 월드 체인이 써 내는 곳. 화면 레이어가 없으면 곧바로 최종 타깃이라 **기존과 완전히 동일한
	// 패스 구성**이 된다(UI 를 안 쓰는 게임에 비용 0).
	//
	// 있으면 중간 텍스처를 거친다. Overlay 가 hFinal 을 쓰면서 아무것도 안 읽으면
	// RWGraph::Compile 의 역방향 도달 분석이 **월드 체인을 통째로 컬링한다** —
	// producerOf 는 그 텍스처의 마지막 writer 만 찾고 거기서 Reads 를 따라 거슬러 올라가므로,
	// Overlay 가 hFinal 의 마지막 writer 가 되는 순간 Base/Blit/Tonemap 이 죽어 UI 만 남는다.
	// 중간 핸들을 읽게 해서 의존을 명시한다.
	const RWTextureHandle hWorldOut = hasScreenLayers ? graph.CreateTexture(sceneDesc) : hFinal;

	RWPassDesc basePass;
	basePass.Name  = "Base";
	basePass.Write = hScene;
	basePass.Execute = [&renderer, &renderScene, viewports, layers, renderTargetSize, outCameraStats, backgroundColor, hScene, gpuTimer, cutoff, captureDrawOrder]
		(IRHICommandContext& ctx, RWGraph& g)
	{
		RenderViewportsInto(ctx, renderer, renderScene, viewports, layers, renderTargetSize, g.Resolve(hScene), outCameraStats, backgroundColor, gpuTimer, cutoff, captureDrawOrder, false, true);
	};
	graph.AddPass(std::move(basePass));

	if (false == lightingActive)
	{
		RWPassDesc blitPass;
		blitPass.Name  = "Blit";
		blitPass.Reads = { hScene };
		blitPass.Write = hWorldOut;
		blitPass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hScene, hWorldOut]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc blit;
			blit.ColorAttachment.Target = g.Resolve(hWorldOut);
			blit.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			blit.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			blit.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			ctx.BeginRenderPass(blit);
			ctx.SetViewport(0.0f, 0.0f, rtW, rtH);
			renderer.SetRenderTargetSize(renderTargetSize);
			forward->BlitFullscreen(ctx, g.Resolve(hScene));
			ctx.EndRenderPass();
		};
		graph.AddPass(std::move(blitPass));
	}
	else
	{
		RWTextureDesc lightDesc;
		lightDesc.Width  = sceneDesc.Width;
		lightDesc.Height = sceneDesc.Height;
		lightDesc.Format = ERHITextureFormat::RGBA16F;   // HDR 누적(미지원 시 RHI 가 RGBA8 폴백)
		const RWTextureHandle hLight = graph.CreateTexture(lightDesc);

		const Color ambientColor{ ambient[0], ambient[1], ambient[2], 1.0f };

		// ── 그림자: 이미지 기반 1D 폴라 섀도맵 (Catalin Zima) ──
		// (1) CastShadow 스프라이트를 월드공간 공유 OccluderMap 에 1회 렌더(알파=차폐).
		// (2) 그림자 라이트(Point/Spot)마다 OccluderMap 을 각도별 최근접 거리로 리덕션 → ShadowAtlas 행.
		// (3) LightMap 패스가 픽셀 (θ,r)로 아틀라스를 샘플해 umbra + 각도블러 penumbra 로 라이트 차폐.
		// Directional 은 점광 대상 아님(그림자 없음, 균일). 오클루더맵은 광원 수와 무관하게 1장 공유.
		const Render2DView primaryCamera = viewports.front().View;
		std::vector<int> rowForLight(lights.size(), -1);   // 라이트→아틀라스 행(-1=무그림자)
		int shadowRowCount = 0;
		for (std::size_t li = 0; li < lights.size(); ++li)
		{
			const Render2DLightDesc& light = lights[li];
			if (light.CastShadows && 0 != light.Type)   // Point(1)/Spot(2) 만
			{
				rowForLight[li] = shadowRowCount++;
			}
		}

		const bool hasShadow = (shadowRowCount > 0);
		RWTextureHandle hOccluder{};
		RWTextureHandle hAtlas{};
		if (hasShadow)
		{
			RWTextureDesc occDesc;
			occDesc.Width  = sceneDesc.Width;
			occDesc.Height = sceneDesc.Height;
			occDesc.Format = ERHITextureFormat::RGBA8;
			hOccluder = graph.CreateTexture(occDesc);

			RWTextureDesc atlasDesc;
			atlasDesc.Width  = 256;
			atlasDesc.Height = static_cast<std::uint32_t>(shadowRowCount);
			atlasDesc.Format = ERHITextureFormat::RGBA8;
			hAtlas = graph.CreateTexture(atlasDesc);

			// (1) OccluderMap — CastShadow 알파 실루엣을 카메라뷰 월드공간 맵에(공유 1장).
			RWPassDesc occPass;
			occPass.Name  = "OccluderMap";
			occPass.Write = hOccluder;
			occPass.Execute = [forward, &renderScene, primaryCamera, renderTargetSize, hOccluder]
				(IRHICommandContext& ctx, RWGraph& g)
			{
				SafePtr<IRHITexture> target = g.Resolve(hOccluder);
				RenderPassDesc clearDesc;
				clearDesc.ColorAttachment.Target = target;
				clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
				clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
				clearDesc.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				ctx.BeginRenderPass(clearDesc);
				ctx.EndRenderPass();

				RenderPassDesc pass;
				pass.ColorAttachment.Target = target;
				pass.ColorAttachment.LoadOp = ERHILoadOp::Load;
				pass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
				ctx.BeginRenderPass(pass);
				const float ow = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
				const float oh = std::max(1.0f, static_cast<float>(renderTargetSize.Height));
				ctx.SetViewport(0.0f, 0.0f, ow, oh);
				forward->SetRenderTargetSize(renderTargetSize);
				forward->SetViewCameraEx(primaryCamera.PosX, primaryCamera.PosY, primaryCamera.OrthoSizeX, primaryCamera.OrthoSize, primaryCamera.CosR, primaryCamera.SinR);
				forward->RenderOccluders(ctx, renderScene);
				ctx.EndRenderPass();
			};
			graph.AddPass(std::move(occPass));

			// (2) ShadowAtlas 리덕션 — 그림자 라이트마다 1행(256×1)에 각도별 최근접 거리.
			std::vector<std::array<float, 4>> reduceRows;   // (row, lightX, lightY, lightRange)
			for (std::size_t li = 0; li < lights.size(); ++li)
			{
				if (rowForLight[li] < 0)
				{
					continue;
				}
				reduceRows.push_back({ static_cast<float>(rowForLight[li]), lights[li].PosX, lights[li].PosY, lights[li].Range });
			}
			RWPassDesc reducePass;
			reducePass.Name  = "ShadowReduce";
			reducePass.Reads = { hOccluder };
			reducePass.Write = hAtlas;
			reducePass.Execute = [forward, primaryCamera, reduceRows, hOccluder, hAtlas]
				(IRHICommandContext& ctx, RWGraph& g)
			{
				SafePtr<IRHITexture> target = g.Resolve(hAtlas);
				RenderPassDesc clearDesc;
				clearDesc.ColorAttachment.Target = target;
				clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
				clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
				clearDesc.ColorAttachment.SetClearColor(1.0f, 1.0f, 1.0f, 1.0f);   // 미차폐=최대거리
				ctx.BeginRenderPass(clearDesc);
				ctx.EndRenderPass();

				SafePtr<IRHITexture> occ = g.Resolve(hOccluder);
				RenderPassDesc pass;
				pass.ColorAttachment.Target = target;
				pass.ColorAttachment.LoadOp = ERHILoadOp::Load;
				pass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
				ctx.BeginRenderPass(pass);
				for (const std::array<float, 4>& r : reduceRows)
				{
					ctx.SetViewport(0.0f, r[0], 256.0f, 1.0f);
					forward->DrawPolarReduction(ctx, occ,
						primaryCamera.PosX, primaryCamera.PosY, primaryCamera.OrthoSizeX, primaryCamera.OrthoSize, primaryCamera.CosR, primaryCamera.SinR,
						r[1], r[2], r[3]);
				}
				ctx.EndRenderPass();
			};
			graph.AddPass(std::move(reducePass));
		}

		RWPassDesc lightPass;
		lightPass.Name  = "LightMap";
		if (hasShadow)
		{
			lightPass.Reads = { hAtlas };   // 아틀라스 소비 → 리덕션/오클루더 패스 컬링 방지.
		}
		lightPass.Write = hLight;
		lightPass.Execute = [forward, viewports, lights, renderTargetSize, ambientColor, hLight, hAtlas, rowForLight, shadowRowCount, hasShadow]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			SafePtr<IRHITexture> target = g.Resolve(hLight);
			const float lrtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
			const float lrtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

			// 앰비언트로 클리어.
			RenderPassDesc clearDesc;
			clearDesc.ColorAttachment.Target = target;
			clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			clearDesc.ColorAttachment.SetClearColor(ambientColor.Data());
			ctx.BeginRenderPass(clearDesc);
			ctx.EndRenderPass();

			if (lights.empty())
			{
				return;   // 앰비언트만(어둡게) — 라이트 draw 없음.
			}

			SafePtr<IRHITexture> atlas;
			if (hasShadow)
			{
				atlas = g.Resolve(hAtlas);
			}

			// 뷰포트별 렉트/뷰로 라이트를 가산 누적(스프라이트 렌더와 동일한 월드 뷰).
			for (const Render2DViewportDesc& viewport : viewports)
			{
				const float vpX = viewport.RectX * lrtW;
				const float vpY = viewport.RectY * lrtH;
				const float vpW = std::max(viewport.RectW * lrtW, 1.0f);
				const float vpH = std::max(viewport.RectH * lrtH, 1.0f);
				const Render2DView& camera = viewport.View;

				RenderPassDesc pass;
				pass.ColorAttachment.Target = target;
				pass.ColorAttachment.LoadOp = ERHILoadOp::Load;
				pass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
				ctx.BeginRenderPass(pass);
				ctx.SetViewport(vpX, vpY, vpW, vpH);
				forward->SetRenderTargetSize(RenderSurfaceSize{ static_cast<int>(vpW), static_cast<int>(vpH) });
				forward->SetViewCameraEx(camera.PosX, camera.PosY, camera.OrthoSizeX, camera.OrthoSize, camera.CosR, camera.SinR);

				for (std::size_t li = 0; li < lights.size(); ++li)
				{
					const Render2DLightDesc& light = lights[li];
					const int row = (li < rowForLight.size()) ? rowForLight[li] : -1;
					SafePtr<IRHITexture> shadowAtlas = (row >= 0) ? atlas : SafePtr<IRHITexture>{};
					const float softness = std::clamp(light.ShadowSoftness, 0.0f, 1.0f);
					if (2 == light.Type)
					{
						forward->DrawLight2DSpot(ctx, light.PosX, light.PosY, light.Range, light.Color, light.Intensity,
							light.DirX, light.DirY, light.InnerAngleRadians, light.OuterAngleRadians, shadowAtlas, row, shadowRowCount, softness);
					}
					else if (1 == light.Type)
					{
						forward->DrawLight2D(ctx, 1, light.PosX, light.PosY, light.Range, light.Color, light.Intensity, shadowAtlas, row, shadowRowCount, softness);
					}
					else
					{
						// Directional — 점광 대상 아님. 균일 방향광(그림자 없음).
						forward->DrawLight2D(ctx, 0, light.PosX, light.PosY, light.Range, light.Color, light.Intensity);
					}
				}
				ctx.EndRenderPass();
			}
		};
		graph.AddPass(std::move(lightPass));

		// Composite 를 HDR(RGBA16F) 중간 타겟에 → Tonemap 이 Reinhard 로 롤오프해 최종 LDR 로.
		// 이렇게 해야 밝은 라이트 중심이 하드 클램프(순백)되지 않는다.
		RWTextureDesc postDesc;
		postDesc.Width  = sceneDesc.Width;
		postDesc.Height = sceneDesc.Height;
		postDesc.Format = ERHITextureFormat::RGBA16F;
		const RWTextureHandle hPost = graph.CreateTexture(postDesc);

		RWPassDesc compositePass;
		compositePass.Name  = "Composite";
		compositePass.Reads = { hScene, hLight };
		compositePass.Write = hPost;
		compositePass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hScene, hLight, hPost]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc composite;
			composite.ColorAttachment.Target = g.Resolve(hPost);
			composite.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			composite.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			composite.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			ctx.BeginRenderPass(composite);
			ctx.SetViewport(0.0f, 0.0f, rtW, rtH);
			renderer.SetRenderTargetSize(renderTargetSize);
			forward->CompositeLighting(ctx, g.Resolve(hScene), g.Resolve(hLight));
			ctx.EndRenderPass();
		};
		graph.AddPass(std::move(compositePass));

		RWPassDesc tonemapPass;
		tonemapPass.Name  = "Tonemap";
		tonemapPass.Reads = { hPost };
		tonemapPass.Write = hWorldOut;
		tonemapPass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hPost, hWorldOut]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc tonemap;
			tonemap.ColorAttachment.Target = g.Resolve(hWorldOut);
			tonemap.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			tonemap.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			tonemap.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			ctx.BeginRenderPass(tonemap);
			ctx.SetViewport(0.0f, 0.0f, rtW, rtH);
			renderer.SetRenderTargetSize(renderTargetSize);
			forward->TonemapFullscreen(ctx, g.Resolve(hPost));
			ctx.EndRenderPass();
		};
		graph.AddPass(std::move(tonemapPass));
	}

	// ── Overlay — 화면 공간 레이어 ─────────────────────────────────────────────
	// **라이팅 이후**에 그린다. Base 에 섞으면 Composite(SceneColor × LightMap)이 UI 에도
	// 곱해져서, 어두운 캔버스에서 체력바가 같이 어두워진다. 게다가 화면 레이어의 뷰는 원점
	// 고정이라 월드 원점 근처의 라이트가 UI 를 국소적으로 밝히는 더 이상한 결과가 난다.
	//
	// 월드 결과를 최종에 한 번 옮기고(이 Blit 이 hWorldOut 의존을 만들어 월드 체인이 컬링되지
	// 않게 하는 장치이기도 하다) 그 위에 화면 레이어를 얹는다.
	if (hasScreenLayers)
	{
		RWPassDesc overlayPass;
		overlayPass.Name  = "Overlay";
		overlayPass.Reads = { hWorldOut };
		overlayPass.Write = hFinal;
		overlayPass.Execute = [forward, &renderer, &renderScene, viewports, layers, renderTargetSize, rtW, rtH, hWorldOut, hFinal, gpuTimer, cutoff, captureDrawOrder]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc blit;
			blit.ColorAttachment.Target = g.Resolve(hFinal);
			blit.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			blit.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			blit.ColorAttachment.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			ctx.BeginRenderPass(blit);
			ctx.SetViewport(0.0f, 0.0f, rtW, rtH);
			renderer.SetRenderTargetSize(renderTargetSize);
			forward->BlitFullscreen(ctx, g.Resolve(hWorldOut));
			ctx.EndRenderPass();

			// 통계는 넘기지 않는다 — Base 가 이미 뷰포트별로 append 했으므로 여기서 또 넣으면
			// 같은 뷰포트가 두 번 잡힌다. 배경색도 없다(클리어는 위 Blit 이 끝냈다).
			RenderViewportsInto(ctx, renderer, renderScene, viewports, layers, renderTargetSize,
				g.Resolve(hFinal), nullptr, nullptr, gpuTimer, cutoff, captureDrawOrder,
				true, false);
		};
		graph.AddPass(std::move(overlayPass));
	}

	graph.Compile(hFinal);
	graph.Execute(commandContext);
	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
}
