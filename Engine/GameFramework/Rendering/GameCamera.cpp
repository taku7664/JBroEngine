#include "pch.h"
#include "GameCamera.h"

#include "Core/RHI/IRHICommandContext.h"
#include "Core/Renderer/Forward2DRenderer.h"   // CForward2DRenderer — 오프스크린 blit 경로
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/IRenderer.h"
#include "GameFramework/Component/Camera2D.h"
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
	std::vector<GameRenderCameraStats>* outCameraStats)
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

	// ── RenderWeave 그래프: Base(카메라 스택 → SceneColor) → Blit(SceneColor → 최종 타겟) ──
	// 패스를 데이터로 선언하면 그래프가 RT 대여/컬링/실행을 담당한다. 이후 라이팅/그림자/포스트는
	// 이 그래프에 패스를 추가하는 것만으로 확장된다.
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

	graph.Compile(hFinal);
	graph.Execute(commandContext);
	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
}
