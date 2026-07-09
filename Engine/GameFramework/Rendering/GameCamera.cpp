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

	// ── RenderWeave Phase 0: 카메라 스택을 오프스크린 SceneColor 에 렌더 후 최종 타겟으로 blit ──
	// Forward2DRenderer 가 아니면(폴백) 기존처럼 최종 타겟에 직접 렌더한다(sceneColor null).
	CForward2DRenderer* forward = dynamic_cast<CForward2DRenderer*>(&renderer);
	SafePtr<IRHITexture> sceneColor = forward
		? forward->AcquireSceneColorTarget(
			static_cast<std::uint32_t>(std::max(1, renderTargetSize.Width)),
			static_cast<std::uint32_t>(std::max(1, renderTargetSize.Height)))
		: SafePtr<IRHITexture>(nullptr);
	// 스택이 그려질 실제 타겟 — SceneColor 있으면 오프스크린, 없으면 최종 타겟.
	const SafePtr<IRHITexture> stackTarget = sceneColor.IsValid() ? sceneColor : renderTarget;

	RenderPassDesc clearDesc;
	clearDesc.ColorAttachment.Target = stackTarget;
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
		renderPassDesc.ColorAttachment.Target = stackTarget;
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

	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);

	// SceneColor → 최종 타겟 풀스크린 blit(no-blend 복사). SceneColor 없으면 스킵(이미 직접 렌더됨).
	if (sceneColor.IsValid() && nullptr != forward)
	{
		RenderPassDesc blitPass;
		blitPass.ColorAttachment.Target = renderTarget;
		blitPass.ColorAttachment.LoadOp = ERHILoadOp::Clear;
		blitPass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
		blitPass.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		commandContext.BeginRenderPass(blitPass);
		commandContext.SetViewport(0.0f, 0.0f, rtW, rtH);
		renderer.SetRenderTargetSize(renderTargetSize);
		forward->BlitFullscreen(commandContext, sceneColor);
		commandContext.EndRenderPass();
	}
}
