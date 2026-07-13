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
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

std::vector<GameRenderCameraDesc> CollectGameRenderCameras(const CGameScene& scene, float renderWidth, float renderHeight)
{
	renderWidth = std::max(renderWidth, 1.0f);
	renderHeight = std::max(renderHeight, 1.0f);

	std::vector<GameRenderCameraDesc> cameras;
	scene.ForEach<Camera2D>(
		[&](const Camera2D& camera)
		{
			const CGameObject* owner = camera.GetOwner().TryGet();
			if (false == IsActiveComponent(camera))
			{
				return;
			}

			const Matrix3x2 worldTransform = GetWorldTransform(*owner);
			const Vector2 posPixel = camera.Position.Resolve(renderWidth, renderHeight);
			Vector2 sizePixel = camera.Size.Resolve(renderWidth, renderHeight);
			sizePixel.x = std::max(sizePixel.x, 1.0f);
			sizePixel.y = std::max(sizePixel.y, 1.0f);

			const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
			const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
			const float safeScaleX = std::max(scaleX, 0.0001f);
			const float safeScaleY = std::max(scaleY, 0.0001f);
			const float baseOrtho = camera.OrthographicSize > 0.0f ? camera.OrthographicSize : 5.0f;
			const float aspect = renderWidth / renderHeight;
			const float cosR = scaleX > 1e-6f ? worldTransform.M11 / scaleX : 1.0f;
			const float sinR = scaleX > 1e-6f ? worldTransform.M12 / scaleX : 0.0f;

			GameRenderCameraDesc desc;
			desc.PosX = worldTransform.Dx;
			desc.PosY = worldTransform.Dy;
			desc.OrthoSize = baseOrtho * safeScaleY;
			desc.OrthoSizeX = baseOrtho * safeScaleX * aspect;
			desc.CosR = cosR;
			desc.SinR = sinR;
			desc.ViewportX = posPixel.x / renderWidth;
			desc.ViewportY = posPixel.y / renderHeight;
			desc.ViewportW = sizePixel.x / renderWidth;
			desc.ViewportH = sizePixel.y / renderHeight;
			desc.ClearColor = Color{
				camera.ClearColor[0],
				camera.ClearColor[1],
				camera.ClearColor[2],
				camera.ClearColor[3] };
			desc.Priority = camera.Priority;
			desc.OwnerObject = owner;
			cameras.push_back(desc);
		});

	std::sort(cameras.begin(), cameras.end(),
		[](const GameRenderCameraDesc& lhs, const GameRenderCameraDesc& rhs)
		{
			return lhs.Priority < rhs.Priority;
		});

	return cameras;
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

namespace
{
	// 카메라 스택(클리어 + 카메라별 렌더)을 지정 타겟에 그린다. 그래프 Base 패스와
	// 폴백 경로가 공유한다. outCameraStats 에 카메라별 통계를 append(호출자가 미리 clear).
	void RenderCameraStackInto(
		IRHICommandContext& commandContext,
		IRenderer& renderer,
		IRenderScene& renderScene,
		const std::vector<GameRenderCameraDesc>& cameras,
		const RenderSurfaceSize& renderTargetSize,
		SafePtr<IRHITexture> target,
		std::vector<GameRenderCameraStats>* outCameraStats)
	{
		const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
		const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

		RenderPassDesc clearDesc;
		clearDesc.ColorAttachment.Target = target;
		clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
		clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
		clearDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		commandContext.BeginRenderPass(clearDesc);
		commandContext.EndRenderPass();

		for (const GameRenderCameraDesc& camera : cameras)
		{
			const float vpX = camera.ViewportX * rtW;
			const float vpY = camera.ViewportY * rtH;
			const float vpW = std::max(camera.ViewportW * rtW, 1.0f);
			const float vpH = std::max(camera.ViewportH * rtH, 1.0f);

			RenderPassDesc renderPassDesc;
			renderPassDesc.ColorAttachment.Target = target;
			renderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Load;
			renderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;

			commandContext.BeginRenderPass(renderPassDesc);
			commandContext.SetViewport(vpX, vpY, vpW, vpH);
			renderer.SetRenderTargetSize(RenderSurfaceSize{ static_cast<int>(vpW), static_cast<int>(vpH) });

			if (camera.ClearColor.A > (1.0f / 255.0f))
			{
				renderer.FillViewportColor(
					camera.ClearColor.R,
					camera.ClearColor.G,
					camera.ClearColor.B,
					camera.ClearColor.A);
			}

			renderer.SetViewCameraEx(
				camera.PosX,
				camera.PosY,
				camera.OrthoSizeX,
				camera.OrthoSize,
				camera.CosR,
				camera.SinR);
			renderer.Render(renderScene);
			if (outCameraStats)
			{
				GameRenderCameraStats stats;
				stats.OwnerObject = camera.OwnerObject;
				stats.Culling = renderer.GetLastCullingStats();
				outCameraStats->push_back(stats);
			}
			commandContext.EndRenderPass();
		}
	}
}

void RenderGameCameraStack(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const std::vector<GameRenderCameraDesc>& cameras,
	const RenderSurfaceSize& renderTargetSize,
	SafePtr<IRHITexture> renderTarget,
	std::vector<GameRenderCameraStats>* outCameraStats,
	const std::vector<GameRenderLightDesc>& lights)
{
	if (outCameraStats)
	{
		outCameraStats->clear();
	}

	if (cameras.empty())
	{
		return;
	}

	const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
	const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

	// Forward2DRenderer 가 아니면 렌더그래프/RT풀이 없으므로 최종 타겟에 직접 렌더(폴백).
	CForward2DRenderer* forward = renderer.AsForward2DRenderer();
	if (nullptr == forward)
	{
		RenderCameraStackInto(commandContext, renderer, renderScene, cameras, renderTargetSize, renderTarget, outCameraStats);
		renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
		return;
	}

	// ── RenderWeave 그래프 ──────────────────────────────────────────────────────
	// 항상: Base(카메라 스택 → SceneColor).
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
	basePass.Execute = [&renderer, &renderScene, &cameras, renderTargetSize, outCameraStats, hScene]
		(IRHICommandContext& ctx, RWGraph& g)
	{
		RenderCameraStackInto(ctx, renderer, renderScene, cameras, renderTargetSize, g.Resolve(hScene), outCameraStats);
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
		const GameRenderCameraDesc primaryCamera = cameras.front();
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
		lightPass.Execute = [forward, &cameras, &lights, renderTargetSize, ambientColor, hLight, hAtlas, rowForLight, shadowRowCount, hasShadow]
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

			// 카메라별 뷰포트/뷰로 라이트를 가산 누적(스프라이트 렌더와 동일한 월드 뷰).
			for (const GameRenderCameraDesc& camera : cameras)
			{
				const float vpX = camera.ViewportX * lrtW;
				const float vpY = camera.ViewportY * lrtH;
				const float vpW = std::max(camera.ViewportW * lrtW, 1.0f);
				const float vpH = std::max(camera.ViewportH * lrtH, 1.0f);

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
