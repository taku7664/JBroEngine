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
#include <cmath>

std::vector<GameRenderCameraDesc> CollectGameRenderCameras(const CGameScene& scene, float renderWidth, float renderHeight)
{
	renderWidth = std::max(renderWidth, 1.0f);
	renderHeight = std::max(renderHeight, 1.0f);

	std::vector<GameRenderCameraDesc> cameras;
	scene.ForEach<Camera2D>(
		[&](const Camera2D& camera)
		{
			const CGameObject* owner = camera.GetOwner();
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
			const CGameObject* owner = light.GetOwner();
			if (nullptr == owner || false == IsActiveComponent(light))
			{
				return;
			}

			const Matrix3x2 worldTransform = GetWorldTransform(*owner);

			GameRenderLightDesc desc;
			// Spot(SpotReady)은 아직 미구현 — Phase 2 에서는 Point 로 취급한다.
			desc.Type = (ELight2DType::Directional == light.Type) ? 0 : 1;
			desc.PosX = worldTransform.Dx;
			desc.PosY = worldTransform.Dy;
			desc.Color[0] = light.Color[0];
			desc.Color[1] = light.Color[1];
			desc.Color[2] = light.Color[2];
			desc.Color[3] = light.Color[3];
			desc.Intensity = light.Intensity;
			desc.Range = light.Range;
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
	CForward2DRenderer* forward = dynamic_cast<CForward2DRenderer*>(&renderer);
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
	const bool ambientIsWhite = ambient[0] >= 1.0f && ambient[1] >= 1.0f && ambient[2] >= 1.0f;
	const bool lightingActive = (false == lights.empty()) || (false == ambientIsWhite);

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

		RWPassDesc lightPass;
		lightPass.Name  = "LightMap";
		lightPass.Write = hLight;
		lightPass.Execute = [forward, &renderer, &cameras, &lights, renderTargetSize, ambientColor, hLight]
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
				renderer.SetRenderTargetSize(RenderSurfaceSize{ static_cast<int>(vpW), static_cast<int>(vpH) });
				renderer.SetViewCameraEx(camera.PosX, camera.PosY, camera.OrthoSizeX, camera.OrthoSize, camera.CosR, camera.SinR);

				for (const GameRenderLightDesc& light : lights)
				{
					forward->DrawLight2D(ctx, light.Type, light.PosX, light.PosY, light.Range, light.Color, light.Intensity);
				}
				ctx.EndRenderPass();
			}
		};
		graph.AddPass(std::move(lightPass));

		RWPassDesc compositePass;
		compositePass.Name  = "Composite";
		compositePass.Reads = { hScene, hLight };
		compositePass.Write = hFinal;
		compositePass.Execute = [forward, &renderer, renderTargetSize, rtW, rtH, hScene, hLight, hFinal]
			(IRHICommandContext& ctx, RWGraph& g)
		{
			RenderPassDesc composite;
			composite.ColorAttachment.Target = g.Resolve(hFinal);
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
	}

	graph.Compile(hFinal);
	graph.Execute(commandContext);
	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
}
