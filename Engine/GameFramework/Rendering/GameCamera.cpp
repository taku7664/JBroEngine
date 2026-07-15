#include "pch.h"
#include "GameCamera.h"

#include "Core/RHI/IRHICommandContext.h"
#include "Core/Renderer/Forward2DRenderer.h"   // CForward2DRenderer — 오프스크린 blit 경로
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/IRenderer.h"
#include "Core/RuntimeConfig.h"                 // 전역 앰비언트
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/GameLayer.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	// 폴백 카메라 — 뷰포트가 카메라를 지목하지 않았을 때 쓸 "첫 활성 카메라".
	// 순서 규약 = 레이어 순서 → 레이어 내 하이라키(생성) 순서. 스크립트 실행 순서와 같은 축이라
	// 예측 가능하다. 풀 순회 순서(비결정)에 의존하지 않는다.
	const Camera2D* FindFallbackCamera(const CGameScene& scene)
	{
		const Camera2D* best = nullptr;
		std::uint16_t bestLayerIndex = 0;
		std::uint64_t bestCreationOrder = 0;
		scene.ForEach<Camera2D>(
			[&](const Camera2D& camera)
			{
				if (false == IsActiveComponent(camera))
				{
					return;
				}
				const CGameObject* owner = camera.GetOwner().TryGet();
				if (nullptr == owner)
				{
					return;
				}
				const std::uint16_t layerIndex = owner->GetLayerIndex();
				const std::uint64_t creationOrder = owner->GetCreationOrder();
				if (nullptr == best
					|| layerIndex < bestLayerIndex
					|| (layerIndex == bestLayerIndex && creationOrder < bestCreationOrder))
				{
					best = &camera;
					bestLayerIndex = layerIndex;
					bestCreationOrder = creationOrder;
				}
			});
		return best;
	}

	// 뷰포트의 카메라 Ref 해석. SafePtr 캐시가 살아 있으면 그대로 쓰고(매 프레임 guid
	// 문자열 파싱 회피), 죽었을 때만 guid 로 재해석한다 — 레이어 재로드로 카메라 오브젝트가
	// 새로 태어나도 같은 guid 면 다시 붙는다.
	const Camera2D* ResolveViewportCamera(CGameScene& scene, CanvasViewport& viewport)
	{
		CGameObject* cameraObject = viewport.ResolvedCamera.TryGet();
		if (nullptr == cameraObject && false == viewport.CameraObjectGuid.IsNull())
		{
			viewport.ResolvedCamera = scene.FindByInstanceGuid(viewport.CameraObjectGuid);
			cameraObject = viewport.ResolvedCamera.TryGet();
		}

		if (cameraObject)
		{
			if (Camera2D* camera = cameraObject->GetComponent<Camera2D>())
			{
				if (IsActiveComponent(*camera))
				{
					return camera;
				}
			}
		}
		return FindFallbackCamera(scene);
	}

	// 뷰포트의 레이어 필터(guid 목록)를 캔버스 순서의 인덱스 목록으로 해석한다.
	// 비어 있으면(대부분) 전체 레이어 — 빈 목록을 그대로 돌려주고 렌더가 전체로 해석한다.
	std::vector<RenderLayerIndex> ResolveViewportLayers(const CGameScene& scene, const CanvasViewport& viewport)
	{
		std::vector<RenderLayerIndex> layerIndices;
		if (viewport.LayerFilter.empty())
		{
			return layerIndices;
		}
		layerIndices.reserve(viewport.LayerFilter.size());
		for (const File::Guid& layerGuid : viewport.LayerFilter)
		{
			if (const CGameLayer* layer = scene.FindLayerByInstanceGuid(layerGuid).TryGet())
			{
				layerIndices.push_back(layer->GetIndex());
			}
		}
		// 캔버스 순서(아래→위)로 그려야 하므로 필터 저작 순서가 아니라 인덱스 순으로 정렬한다.
		std::sort(layerIndices.begin(), layerIndices.end());
		return layerIndices;
	}
}

std::vector<GameRenderViewportDesc> CollectGameRenderViewports(CGameScene& scene, float renderWidth, float renderHeight)
{
	renderWidth = std::max(renderWidth, 1.0f);
	renderHeight = std::max(renderHeight, 1.0f);

	// 뷰포트를 저작하지 않은 캔버스(대부분)는 풀스크린 기본 뷰포트 1개로 그린다.
	scene.GetOrCreateDefaultViewport();

	std::vector<GameRenderViewportDesc> viewports;
	viewports.reserve(scene.GetViewportCount());
	for (std::size_t i = 0; i < scene.GetViewportCount(); ++i)
	{
		CanvasViewport* viewport = scene.GetViewportAt(i);
		if (nullptr == viewport || false == viewport->Active)
		{
			continue;
		}

		const Camera2D* camera = ResolveViewportCamera(scene, *viewport);
		if (nullptr == camera)
		{
			continue;   // 눈이 없으면 그릴 수 없다.
		}
		const CGameObject* owner = camera->GetOwner().TryGet();
		if (nullptr == owner)
		{
			continue;
		}

		const Vector2 posPixel = viewport->Position.Resolve(renderWidth, renderHeight);
		Vector2 sizePixel = viewport->Size.Resolve(renderWidth, renderHeight);
		sizePixel.x = std::max(sizePixel.x, 1.0f);
		sizePixel.y = std::max(sizePixel.y, 1.0f);

		const Matrix3x2 worldTransform = GetWorldTransform(*owner);
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float safeScaleX = std::max(scaleX, 0.0001f);
		const float safeScaleY = std::max(scaleY, 0.0001f);
		const float baseOrtho = camera->OrthographicSize > 0.0f ? camera->OrthographicSize : 5.0f;
		// 종횡비는 화면 전체가 아니라 이 뷰포트의 렉트 기준 — 스플릿(좌/우 반쪽)에서
		// 화면 전체 비율을 쓰면 내용이 찌그러진다.
		const float aspect = sizePixel.x / std::max(sizePixel.y, 1.0f);

		GameRenderViewportDesc desc;
		desc.PosX = worldTransform.Dx;
		desc.PosY = worldTransform.Dy;
		desc.OrthoSize = baseOrtho * safeScaleY;
		desc.OrthoSizeX = baseOrtho * safeScaleX * aspect;
		desc.CosR = scaleX > 1e-6f ? worldTransform.M11 / scaleX : 1.0f;
		desc.SinR = scaleX > 1e-6f ? worldTransform.M12 / scaleX : 0.0f;
		desc.RectX = posPixel.x / renderWidth;
		desc.RectY = posPixel.y / renderHeight;
		desc.RectW = sizePixel.x / renderWidth;
		desc.RectH = sizePixel.y / renderHeight;
		desc.Layers = ResolveViewportLayers(scene, *viewport);
		desc.CameraOwnerObject = owner;
		viewports.push_back(std::move(desc));
	}

	return viewports;
}

std::vector<GameRenderLightDesc> CollectGameRenderLights(const CGameScene& scene)
{
	std::vector<GameRenderLightDesc> lights;
	scene.ForEach<Light2D>(
		[&](const Light2D& light)
		{
			const CGameObject* owner = light.GetOwner().TryGet();
			if (nullptr == owner || false == IsActiveComponent(light))
			{
				return;
			}

			const Matrix3x2 worldTransform = GetWorldTransform(*owner);
			// 방향 = 로컬 +X 의 월드 방향(회전에서). 씬뷰 기즈모와 동일 규약.
			const float dirLen = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
			const float dirX = dirLen > 1e-6f ? worldTransform.M11 / dirLen : 1.0f;
			const float dirY = dirLen > 1e-6f ? worldTransform.M12 / dirLen : 0.0f;

			GameRenderLightDesc desc;
			desc.Type = (ELight2DType::Directional == light.Type) ? 0
			          : (ELight2DType::Spot == light.Type)        ? 2
			          :                                             1;
			desc.PosX = worldTransform.Dx;
			desc.PosY = worldTransform.Dy;
			desc.Color[0] = light.Color[0];
			desc.Color[1] = light.Color[1];
			desc.Color[2] = light.Color[2];
			desc.Color[3] = light.Color[3];
			desc.Intensity = light.Intensity;
			desc.Range = light.Range;
			desc.DirX = dirX;
			desc.DirY = dirY;
			desc.InnerAngleRadians = light.InnerAngleRadians;
			desc.OuterAngleRadians = light.OuterAngleRadians;
			// 그림자: Point·Spot = 라이트 중심 occluder + 방사 레이마치, Directional = 카메라뷰 occluder
			// + 고정방향 레이마치. 셋 다 지원(그래프가 타입별로 occluder/draw 분기).
			desc.CastShadows = light.CastShadows;
			desc.ShadowLength = light.ShadowLength;
			desc.ShadowSoftness = light.ShadowSoftness;
			lights.push_back(desc);
		});

	return lights;
}

std::vector<GameRenderLayerDesc> CollectGameRenderLayers(const CGameScene& scene, bool forceOwnTextureAll)
{
	std::vector<GameRenderLayerDesc> layers;
	const std::size_t layerCount = scene.GetLayerCount();
	layers.reserve(layerCount);
	for (std::size_t i = 0; i < layerCount; ++i)
	{
		const CGameLayer* layer = scene.GetLayerAt(i);
		if (nullptr == layer)
		{
			continue;
		}

		GameRenderLayerDesc desc;
		desc.Index = layer->GetIndex();
		switch (layer->BlendMode)
		{
		case ELayerBlendMode::Additive: desc.BlendMode = ERHIBlendMode::LayerAdditive; break;
		case ELayerBlendMode::Multiply: desc.BlendMode = ERHIBlendMode::LayerMultiply; break;
		case ELayerBlendMode::Screen:   desc.BlendMode = ERHIBlendMode::LayerScreen;   break;
		case ELayerBlendMode::Normal:
		default:                        desc.BlendMode = ERHIBlendMode::LayerNormal;   break;
		}
		desc.Opacity = std::clamp(layer->Opacity, 0.0f, 1.0f);
		desc.ParallaxFactor = layer->ParallaxFactor;
		desc.Visible = layer->Visible;
		desc.Static = layer->Static;
		desc.ForceOwnTexture = layer->ForceOwnTexture;
		// 자기 RT 가 필요한 조건 — 이 중 아무것도 아니면 대상에 직접 그린다(RT 왕복 없음).
		// Normal + Opacity 1 + 비Static 레이어(대다수)는 레이어 도입 전과 동일 비용이 된다.
		desc.NeedsOwnTexture = forceOwnTextureAll
			|| layer->ForceOwnTexture
			|| layer->Static
			|| desc.Opacity < 1.0f
			|| ELayerBlendMode::Normal != layer->BlendMode;
		layers.push_back(desc);
	}
	return layers;
}

namespace
{
	// 카메라 스택(클리어 + 카메라별 렌더)을 지정 타겟에 그린다. 그래프 Base 패스와
	// 폴백 경로가 공유한다. outCameraStats 에 카메라별 통계를 append(호출자가 미리 clear).
	// 레이어 뷰 = 뷰포트 카메라 뷰에 ParallaxFactor 적용. 현재는 "위치만 배율"이다
	// (factor 1 = 카메라와 동일, 0.5 = 절반 속도 원경, 0 = 월드 원점 고정).
	// 회전·줌은 카메라를 그대로 따른다 — factor 0 레이어를 화면 완전 고정(UI)으로 만들려면
	// 회전·줌 독립까지 필요하며, 그건 설계 문서에 미결로 남은 항목이다.
	GameRenderViewportDesc ApplyLayerParallax(const GameRenderViewportDesc& viewport, float parallaxFactor)
	{
		if (1.0f == parallaxFactor)
		{
			return viewport;
		}
		GameRenderViewportDesc view = viewport;
		view.PosX = viewport.PosX * parallaxFactor;
		view.PosY = viewport.PosY * parallaxFactor;
		return view;
	}

	// 한 레이어를 현재 바인딩된 타겟(뷰포트 설정 완료 상태)에 그린다.
	// drawAllItems = 레이어 구간이 아니라 씬 전체를 그린다(레이어 스냅샷이 없는 폴백 경로).
	void DrawLayerItems(
		IRenderer& renderer,
		CForward2DRenderer* forward,
		IRenderScene& renderScene,
		const GameRenderViewportDesc& view,
		const GameRenderLayerDesc& layer,
		bool drawAllItems)
	{
		renderer.SetViewCameraEx(view.PosX, view.PosY, view.OrthoSizeX, view.OrthoSize, view.CosR, view.SinR);
		if (forward && false == drawAllItems)
		{
			forward->RenderLayer(renderScene, layer.Index);
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
	//   · 평범한 레이어(Normal+불투명+비Static) → 타겟에 직접 드로우(RT 왕복 없음)
	//   · 그 외                                → 스크래치 RT 에 그린 뒤 블렌드·Opacity 로 합성
	// 같은 레이어가 뷰포트마다 각자의 눈으로 다시 그려지는 것이 멀티뷰포트의 본체다.
	// outCameraStats 에 뷰포트별 통계를 append(호출자가 미리 clear).
	void RenderViewportsInto(
		IRHICommandContext& commandContext,
		IRenderer& renderer,
		IRenderScene& renderScene,
		const std::vector<GameRenderViewportDesc>& viewports,
		const std::vector<GameRenderLayerDesc>& layers,
		const RenderSurfaceSize& renderTargetSize,
		SafePtr<IRHITexture> target,
		std::vector<GameRenderCameraStats>* outCameraStats,
		const float* backgroundColor)
	{
		const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
		const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

		// 컴포짓 바탕 = 캔버스 배경색(없으면 투명). 레이어 RT 는 투명으로 클리어되고
		// 이 위에 순서대로 얹힌다 — 구 카메라별 ClearColor 를 캔버스급으로 승계한 것.
		RenderPassDesc clearDesc;
		clearDesc.ColorAttachment.Target = target;
		clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
		clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
		clearDesc.ColorAttachment.ClearColor = backgroundColor
			? Color{ backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3] }
			: Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		commandContext.BeginRenderPass(clearDesc);
		commandContext.EndRenderPass();

		CForward2DRenderer* forward = renderer.AsForward2DRenderer();

		// 레이어 스냅샷이 없으면(레이어를 안 넘긴 호출자) 씬 전체를 한 레이어처럼 그린다 —
		// 조용히 레이어 0 만 그리는 사고를 막는 폴백.
		const bool drawAllItems = layers.empty();
		const std::vector<GameRenderLayerDesc> implicitLayers(drawAllItems ? 1 : 0);
		const std::vector<GameRenderLayerDesc>& effectiveLayers = drawAllItems ? implicitLayers : layers;

		for (const GameRenderViewportDesc& viewport : viewports)
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
			for (const GameRenderLayerDesc& layer : effectiveLayers)
			{
				if (false == layer.Visible)
				{
					continue;
				}
				// 레이어 필터 — 비면 전체 레이어(대부분). 목록이 작아 선형 탐색으로 충분하다.
				if (false == viewport.Layers.empty()
					&& std::find(viewport.Layers.begin(), viewport.Layers.end(), layer.Index) == viewport.Layers.end())
				{
					continue;
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

				const GameRenderViewportDesc view = ApplyLayerParallax(viewport, layer.ParallaxFactor);

				if (false == layer.NeedsOwnTexture || nullptr == forward)
				{
					// 직접 경로 — 레이어 도입 전과 동일한 드로우 비용.
					commandContext.BeginRenderPass(renderPassDesc);
					commandContext.SetViewport(vpX, vpY, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					DrawLayerItems(renderer, forward, renderScene, view, layer, drawAllItems);
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
					layerClear.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
					commandContext.BeginRenderPass(layerClear);
					commandContext.EndRenderPass();

					RenderPassDesc layerPass;
					layerPass.ColorAttachment.Target = scratch;
					layerPass.ColorAttachment.LoadOp = ERHILoadOp::Load;
					layerPass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
					commandContext.BeginRenderPass(layerPass);
					commandContext.SetViewport(0.0f, 0.0f, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					DrawLayerItems(renderer, forward, renderScene, view, layer, drawAllItems);
					commandContext.EndRenderPass();

					commandContext.BeginRenderPass(renderPassDesc);
					commandContext.SetViewport(vpX, vpY, vpW, vpH);
					renderer.SetRenderTargetSize(viewportSize);
					forward->CompositeLayer(commandContext, scratch, layer.BlendMode, layer.Opacity);
					commandContext.EndRenderPass();
				}

				const RenderCullingStats layerStats = renderer.GetLastCullingStats();
				cameraStats.SubmittedCount += layerStats.SubmittedCount;
				cameraStats.DrawnCount += layerStats.DrawnCount;
				cameraStats.CulledCount += layerStats.CulledCount;
			}

			if (outCameraStats)
			{
				GameRenderCameraStats stats;
				stats.OwnerObject = viewport.CameraOwnerObject;
				stats.Culling = cameraStats;
				outCameraStats->push_back(stats);
			}
		}
	}
}

void RenderGameViewports(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const std::vector<GameRenderViewportDesc>& viewports,
	const RenderSurfaceSize& renderTargetSize,
	SafePtr<IRHITexture> renderTarget,
	std::vector<GameRenderCameraStats>* outCameraStats,
	const std::vector<GameRenderLightDesc>& lights,
	const std::vector<GameRenderLayerDesc>& layers,
	const float* backgroundColor)
{
	if (outCameraStats)
	{
		outCameraStats->clear();
	}

	if (viewports.empty())
	{
		return;
	}

	const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
	const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

	// Forward2DRenderer 가 아니면 렌더그래프/RT풀이 없으므로 최종 타겟에 직접 렌더(폴백).
	CForward2DRenderer* forward = renderer.AsForward2DRenderer();
	if (nullptr == forward)
	{
		RenderViewportsInto(commandContext, renderer, renderScene, viewports, layers, renderTargetSize, renderTarget, outCameraStats, backgroundColor);
		renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
		return;
	}

	// ── RenderWeave 그래프 ──────────────────────────────────────────────────────
	// 항상: Base(뷰포트 × 레이어 → SceneColor).
	// 라이팅 비활성(라이트 0 + 앰비언트 백색): Blit(SceneColor → 최종). 화면 불변.
	// 라이팅 활성: LightMap(앰비언트 클리어 + Light2D 가산) → Composite(SceneColor × LightMap → 최종).
	// 패스를 데이터로 선언하면 그래프가 RT 대여/컬링/실행을 담당한다.
	const float* ambient = Runtime.AmbientLight;
	// 라이트가 있을 때만 라이팅 경로. 라이트0 씬 = 패스스루(앰비언트 무관, 화면 불변).
	const bool lightingActive = (false == lights.empty());

	RWGraph graph(forward->GetRenderWeavePool());
	const RWTextureHandle hFinal = graph.ImportTexture(renderTarget);

	RWTextureDesc sceneDesc;
	sceneDesc.Width  = static_cast<std::uint32_t>(std::max(1, renderTargetSize.Width));
	sceneDesc.Height = static_cast<std::uint32_t>(std::max(1, renderTargetSize.Height));
	sceneDesc.Format = ERHITextureFormat::RGBA8;
	const RWTextureHandle hScene = graph.CreateTexture(sceneDesc);

	RWPassDesc basePass;
	basePass.Name  = "Base";
	basePass.Write = hScene;
	basePass.Execute = [&renderer, &renderScene, &viewports, &layers, renderTargetSize, outCameraStats, backgroundColor, hScene]
		(IRHICommandContext& ctx, RWGraph& g)
	{
		RenderViewportsInto(ctx, renderer, renderScene, viewports, layers, renderTargetSize, g.Resolve(hScene), outCameraStats, backgroundColor);
	};
	graph.AddPass(std::move(basePass));

	if (false == lightingActive)
	{
		RWPassDesc blitPass;
		blitPass.Name  = "Blit";
		blitPass.Reads = { hScene };
		blitPass.Write = hFinal;
		blitPass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hScene, hFinal]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc blit;
			blit.ColorAttachment.Target = g.Resolve(hFinal);
			blit.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			blit.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			blit.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
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
		const GameRenderViewportDesc primaryCamera = viewports.front();
		std::vector<int> rowForLight(lights.size(), -1);   // 라이트→아틀라스 행(-1=무그림자)
		int shadowRowCount = 0;
		for (std::size_t li = 0; li < lights.size(); ++li)
		{
			const GameRenderLightDesc& light = lights[li];
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
				clearDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
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
				clearDesc.ColorAttachment.ClearColor = Color{ 1.0f, 1.0f, 1.0f, 1.0f };   // 미차폐=최대거리
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
		lightPass.Execute = [forward, &viewports, &lights, renderTargetSize, ambientColor, hLight, hAtlas, rowForLight, shadowRowCount, hasShadow]
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
			clearDesc.ColorAttachment.ClearColor = ambientColor;
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
			for (const GameRenderViewportDesc& camera : viewports)
			{
				const float vpX = camera.RectX * lrtW;
				const float vpY = camera.RectY * lrtH;
				const float vpW = std::max(camera.RectW * lrtW, 1.0f);
				const float vpH = std::max(camera.RectH * lrtH, 1.0f);

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
					const GameRenderLightDesc& light = lights[li];
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
			composite.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
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
		tonemapPass.Write = hFinal;
		tonemapPass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hPost, hFinal]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc tonemap;
			tonemap.ColorAttachment.Target = g.Resolve(hFinal);
			tonemap.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			tonemap.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			tonemap.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
			ctx.BeginRenderPass(tonemap);
			ctx.SetViewport(0.0f, 0.0f, rtW, rtH);
			renderer.SetRenderTargetSize(renderTargetSize);
			forward->TonemapFullscreen(ctx, g.Resolve(hPost));
			ctx.EndRenderPass();
		};
		graph.AddPass(std::move(tonemapPass));
	}

	graph.Compile(hFinal);
	graph.Execute(commandContext);
	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
}
