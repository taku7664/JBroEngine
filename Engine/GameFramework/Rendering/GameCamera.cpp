#include "pch.h"
#include "GameCamera.h"

#include "Core/RHI/IRHICommandContext.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/IRenderer.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <cmath>

std::vector<GameRenderCameraDesc> CollectGameRenderCameras(const CScene& scene, float renderWidth, float renderHeight)
{
	renderWidth = std::max(renderWidth, 1.0f);
	renderHeight = std::max(renderHeight, 1.0f);

	std::vector<GameRenderCameraDesc> cameras;
	scene.ForEach<GameObject, Transform2D, Camera2D>(
		[&](EntityId entity, const GameObject& gameObject, const Transform2D&, const Camera2D& camera)
		{
			if (false == gameObject.IsActive || false == camera.IsEnabled)
			{
				return;
			}

			const Matrix3x2 worldTransform = GetWorldTransform(scene, entity);
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
			desc.IsMainCamera = camera.IsMainCamera;
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
	SafePtr<IRHITexture> renderTarget)
{
	if (cameras.empty())
	{
		return;
	}

	RenderPassDesc clearDesc;
	clearDesc.ColorAttachment.Target = renderTarget;
	clearDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
	clearDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
	clearDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
	commandContext.BeginRenderPass(clearDesc);
	commandContext.EndRenderPass();

	const float rtW = std::max(1.0f, static_cast<float>(renderTargetSize.Width));
	const float rtH = std::max(1.0f, static_cast<float>(renderTargetSize.Height));

	for (const GameRenderCameraDesc& camera : cameras)
	{
		const float vpX = camera.ViewportX * rtW;
		const float vpY = camera.ViewportY * rtH;
		const float vpW = std::max(camera.ViewportW * rtW, 1.0f);
		const float vpH = std::max(camera.ViewportH * rtH, 1.0f);

		RenderPassDesc renderPassDesc;
		renderPassDesc.ColorAttachment.Target = renderTarget;
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
		commandContext.EndRenderPass();
	}

	renderer.SetViewCamera(0.0f, 0.0f, 1.0f);
}
