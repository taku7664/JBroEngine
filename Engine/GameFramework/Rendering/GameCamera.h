#pragma once

#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHICommandTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <vector>

class CScene;
class IRHICommandContext;
class IRHITexture;
class IRenderer;
class IRenderScene;

struct GameRenderCameraDesc
{
	float PosX = 0.0f;
	float PosY = 0.0f;
	float OrthoSize = 5.0f;
	float OrthoSizeX = 5.0f;
	float CosR = 1.0f;
	float SinR = 0.0f;
	float ViewportX = 0.0f;
	float ViewportY = 0.0f;
	float ViewportW = 1.0f;
	float ViewportH = 1.0f;
	Color ClearColor = Color{ 0.08f, 0.09f, 0.11f, 1.0f };
	std::int32_t Priority = 0;
	bool IsMainCamera = false;
};

std::vector<GameRenderCameraDesc> CollectGameRenderCameras(const CScene& scene, float renderWidth, float renderHeight);

void RenderGameCameraStack(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const std::vector<GameRenderCameraDesc>& cameras,
	const RenderSurfaceSize& renderTargetSize,
	SafePtr<IRHITexture> renderTarget = nullptr);
