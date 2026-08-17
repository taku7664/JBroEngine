#include "pch.h"
#include "GameCamera.h"

#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasTransformUtils.h"
#include "GameFramework/Canvas/CanvasViewProjection.h"   // ComputeCanvasViewProjection — 뷰 파라미터 공용 계산

#include <algorithm>
#include <cmath>

namespace
{
	// 폴백 카메라("첫 활성 카메라")는 입력 역투영(ScreenToWorld)도 같은 규약을 써야 하므로
	// CGameCanvas::FindFallbackCamera 로 승격했다 — 여기서 재현하면 두 경로가 어긋난다.

	// 뷰포트의 카메라 Ref 해석. SafePtr 캐시가 살아 있으면 그대로 쓰고(매 프레임 guid
	// 문자열 파싱 회피), 죽었을 때만 guid 로 재해석한다 — 레이어 재로드로 카메라 오브젝트가
	// 새로 태어나도 같은 guid 면 다시 붙는다.
	const Camera2D* ResolveViewportCamera(CGameCanvas& canvas, CanvasViewport& viewport)
	{
		CGameObject* cameraObject = viewport.ResolvedCamera.TryGet();
		if (nullptr == cameraObject && false == viewport.CameraObjectGuid.IsNull())
		{
			viewport.ResolvedCamera = canvas.FindByInstanceGuid(viewport.CameraObjectGuid);
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
		return canvas.FindFallbackCamera();
	}

	// 뷰포트의 레이어 필터(guid 목록)를 캔버스 순서의 인덱스 목록으로 해석해 out 에 채운다.
	// 비어 있으면(대부분) 전체 레이어 — 빈 목록을 그대로 두고 렌더가 전체로 해석한다.
	// out 을 재사용하므로 필터를 안 쓰는 흔한 경우엔 할당이 전혀 없다.
	void ResolveViewportLayers(const CGameCanvas& canvas, const CanvasViewport& viewport,
		std::vector<RenderLayerIndex>& layerIndices)
	{
		layerIndices.clear();
		if (viewport.LayerFilter.empty())
		{
			return;
		}
		layerIndices.reserve(viewport.LayerFilter.size());
		for (const File::Guid& layerGuid : viewport.LayerFilter)
		{
			if (const CGameLayer* layer = canvas.FindLayerByInstanceGuid(layerGuid).TryGet())
			{
				layerIndices.push_back(layer->GetIndex());
			}
		}
		// 캔버스 순서(아래→위)로 그려야 하므로 필터 저작 순서가 아니라 인덱스 순으로 정렬한다.
		std::sort(layerIndices.begin(), layerIndices.end());
	}
}

void CollectGameRenderViewports(CGameCanvas& canvas, float renderWidth, float renderHeight,
	std::vector<Render2DViewportDesc>& outViewports)
{
	renderWidth = std::max(renderWidth, 1.0f);
	renderHeight = std::max(renderHeight, 1.0f);

	// 여기서 캔버스에 렌더 해상도를 기록하지 않는다 — 이 수집기는 게임 화면 말고도
	// 레이어 썸네일(작은 RT)·"활성 카메라 있나" 판정에서도 불린다. 여기서 기록하면 마지막
	// 호출자(대개 썸네일)의 크기가 남아 ScreenToWorld 가 22x30 같은 값을 기준으로 삼는다.
	// 기록은 "플레이어가 보는 표면"을 아는 호출자(패키지 게임 루프 / 에디터 게임뷰)가 한다.

	// 뷰포트를 저작하지 않은 캔버스(대부분)는 풀스크린 기본 뷰포트 1개로 그린다.
	canvas.GetOrCreateDefaultViewport();

	// clear() 대신 쓰기 인덱스로 앞에서부터 덮어쓴다 — clear 는 각 desc 의 Layers 벡터까지
	// 파괴해 레이어 필터를 쓰는 뷰포트가 매 프레임 다시 할당하게 된다. 살아남는 앞부분은
	// 바깥 버퍼와 안쪽 Layers 버퍼가 모두 유지된다.
	std::size_t writeIndex = 0;
	outViewports.reserve(canvas.GetViewportCount());
	for (std::size_t i = 0; i < canvas.GetViewportCount(); ++i)
	{
		CanvasViewport* viewport = canvas.GetViewportAt(i);
		if (nullptr == viewport || false == viewport->Active)
		{
			continue;
		}

		const Camera2D* camera = ResolveViewportCamera(canvas, *viewport);
		if (nullptr == camera)
		{
			continue;   // 눈이 없으면 그릴 수 없다.
		}
		const CGameObject* owner = camera->GetOwner().TryGet();
		if (nullptr == owner)
		{
			continue;
		}

		// 카메라 뷰 파라미터 계산은 입력 역투영(CGameCanvas::ScreenToWorld)과 공유한다 —
		// 여기서 desc 를 계산하는 방식과 역투영이 어긋나면 마우스 피킹이 조용히 빗나간다.
		const CanvasViewProjection view = ComputeCanvasViewProjection(*viewport, *camera, *owner, renderWidth, renderHeight);

		if (outViewports.size() <= writeIndex)
		{
			outViewports.emplace_back();
		}
		Render2DViewportDesc& desc = outViewports[writeIndex];
		desc.View.PosX = view.PosX;
		desc.View.PosY = view.PosY;
		desc.View.OrthoSize = view.OrthoSize;
		desc.View.OrthoSizeX = view.OrthoSizeX;
		desc.View.CosR = view.CosR;
		desc.View.SinR = view.SinR;
		desc.RectX = view.RectPixelX / renderWidth;
		desc.RectY = view.RectPixelY / renderHeight;
		desc.RectW = view.RectPixelW / renderWidth;
		desc.RectH = view.RectPixelH / renderHeight;
		ResolveViewportLayers(canvas, *viewport, desc.Layers);
		desc.CameraOwnerObject = owner;
		++writeIndex;
	}

	// 이번에 안 쓴 뒷부분은 잘라낸다(용량은 유지).
	outViewports.resize(writeIndex);
}

void CollectGameRenderLights(const CGameCanvas& canvas, std::vector<Render2DLightDesc>& outLights)
{
	std::vector<Render2DLightDesc>& lights = outLights;
	lights.clear();
	canvas.ForEach<Light2D>(
		[&](const Light2D& light)
		{
			const CGameObject* owner = light.GetOwner().TryGet();
			if (nullptr == owner || false == IsActiveComponent(light))
			{
				return;
			}

			const Matrix3x2 worldTransform = GetWorldTransform(*owner);
			// 방향 = 로컬 +X 의 월드 방향(회전에서). 캔버스뷰 기즈모와 동일 규약.
			const float dirLen = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
			const float dirX = dirLen > 1e-6f ? worldTransform.M11 / dirLen : 1.0f;
			const float dirY = dirLen > 1e-6f ? worldTransform.M12 / dirLen : 0.0f;

			Render2DLightDesc desc;
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

}

void CollectGameRenderLayers(const CGameCanvas& canvas, bool forceOwnTextureAll,
	std::vector<Render2DLayerDesc>& outLayers)
{
	std::vector<Render2DLayerDesc>& layers = outLayers;
	layers.clear();
	const std::size_t layerCount = canvas.GetLayerCount();
	layers.reserve(layerCount);
	for (std::size_t i = 0; i < layerCount; ++i)
	{
		const CGameLayer* layer = canvas.GetLayerAt(i);
		if (nullptr == layer)
		{
			continue;
		}

		Render2DLayerDesc desc;
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
		// 저작 개념(ELayerSpace)을 렌더가 쓰는 형태로 접는다 — 파이프라인은 "화면에서 뷰를
		// 뽑고 라이팅 뒤에 그린다" 는 사실만 알면 된다.
		desc.ScreenSpace = (ELayerSpace::Screen == layer->Space);
		desc.ScaleMode = layer->ScaleMode;
		desc.Visible = layer->Visible;
		// 자기 RT 가 필요한 조건 — 이 중 아무것도 아니면 대상에 직접 그린다(RT 왕복 없음).
		// Normal + Opacity 1 인 레이어(대다수)는 레이어 도입 전과 동일 비용이 된다.
		desc.NeedsOwnTexture = forceOwnTextureAll
			|| layer->ForceOwnTexture
			|| desc.Opacity < 1.0f
			|| ELayerBlendMode::Normal != layer->BlendMode;
		layers.push_back(desc);
	}
}
