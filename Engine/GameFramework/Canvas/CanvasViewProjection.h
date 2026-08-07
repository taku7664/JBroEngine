#pragma once

#include "GameFramework/Canvas/CanvasViewport.h"
#include "GameFramework/Canvas/CanvasTransformUtils.h"   // GetWorldTransform
#include "GameFramework/Canvas/GameLayer.h"              // ELayerSpace / EScreenScaleMode
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Object/GameObject.h"
#include "Utillity/Math/Matrix3x2.h"
#include "Utillity/Math/Vector2T.h"

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  CanvasViewProjection ─ 뷰포트 1개의 카메라 뷰 파라미터(RHI 비의존).
//
//  카메라 오너 트랜스폼 + OrthographicSize + 뷰포트 렉트를 하나로 해석한 결과다.
//  같은 계산을 두 곳이 쓴다:
//    · CollectGameRenderViewports (렌더 수집) — GameRenderViewportDesc 를 채운다.
//    · CGameCanvas::ScreenToWorld (입력 역투영) — 스크린 픽셀을 월드로 되돌린다.
//  둘이 계산을 각자 재현하면 역투영이 렌더와 미세하게 어긋난다(마우스 피킹이 조용히 빗나간다).
//  그래서 한 함수(ComputeCanvasViewProjection)로 뽑고 양쪽이 공유한다.
//
//  이 헤더는 RHI/렌더러 타입을 전혀 의존하지 않는다 — GameFramework 헤더만 쓰므로
//  게임 스크립트 DLL 이 링크하는 Canvas.obj 에서 include 해도 링크 클로저를 오염시키지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
struct CanvasViewProjection
{
	// 카메라 뷰(월드 공간).
	float PosX = 0.0f;        // 카메라 월드 위치 x
	float PosY = 0.0f;        // 카메라 월드 위치 y
	float OrthoSize = 5.0f;   // 세로 반높이(HalfH, 월드 유닛)
	float OrthoSizeX = 5.0f;  // 가로 반폭(HalfW, 월드 유닛) — 뷰포트 종횡비 반영
	float CosR = 1.0f;        // 카메라 회전 코사인
	float SinR = 0.0f;        // 카메라 회전 사인

	// 출력 렉트(렌더 타깃 픽셀, 좌상단 원점).
	float RectPixelX = 0.0f;
	float RectPixelY = 0.0f;
	float RectPixelW = 1.0f;
	float RectPixelH = 1.0f;
};

// 카메라 오너 트랜스폼 + OrthographicSize + 뷰포트 렉트 → 뷰 파라미터.
// renderWidth/Height 는 렌더 타깃 픽셀 크기(뷰포트 Layout2D 를 픽셀로 Resolve 하는 기준).
inline CanvasViewProjection ComputeCanvasViewProjection(
	const CanvasViewport& viewport,
	const Camera2D& camera,
	const CGameObject& cameraOwner,
	float renderWidth,
	float renderHeight)
{
	CanvasViewProjection view;

	const Vector2 posPixel = viewport.Position.Resolve(renderWidth, renderHeight);
	Vector2 sizePixel = viewport.Size.Resolve(renderWidth, renderHeight);
	sizePixel.x = std::max(sizePixel.x, 1.0f);
	sizePixel.y = std::max(sizePixel.y, 1.0f);

	const Matrix3x2 worldTransform = GetWorldTransform(cameraOwner);
	const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
	const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
	const float safeScaleX = std::max(scaleX, 0.0001f);
	const float safeScaleY = std::max(scaleY, 0.0001f);
	const float baseOrtho = camera.OrthographicSize > 0.0f ? camera.OrthographicSize : 5.0f;
	// 종횡비는 화면 전체가 아니라 이 뷰포트의 렉트 기준 — 스플릿(좌/우 반쪽)에서
	// 화면 전체 비율을 쓰면 내용이 찌그러진다.
	const float aspect = sizePixel.x / std::max(sizePixel.y, 1.0f);

	view.PosX = worldTransform.Dx;
	view.PosY = worldTransform.Dy;
	view.OrthoSize = baseOrtho * safeScaleY;
	view.OrthoSizeX = baseOrtho * safeScaleX * aspect;
	view.CosR = scaleX > 1e-6f ? worldTransform.M11 / scaleX : 1.0f;
	view.SinR = scaleX > 1e-6f ? worldTransform.M12 / scaleX : 0.0f;
	view.RectPixelX = posPixel.x;
	view.RectPixelY = posPixel.y;
	view.RectPixelW = sizePixel.x;
	view.RectPixelH = sizePixel.y;

	return view;
}

// ─────────────────────────────────────────────────────────────────────────────
//  화면 공간(ELayerSpace::Screen) 투영
//
//  Screen 레이어는 "원점에 못박힌 무회전 가상 카메라"로 그린다. 다른 건 아무것도 안 바뀐다 —
//  컬링·정렬·배칭·텍스트가 전부 월드와 같은 코드로 돈다. 달라지는 건 뷰 파라미터뿐이다.
//
//  **Runtime(RuntimeConfig) 을 여기서 읽지 않는다.** RuntimeConfig 는 호스트 전용이고
//  이 헤더는 게임 DLL 이 링크하는 Canvas.obj 가 include 한다 — DLL 안에서는 그 전역이
//  채워지지 않아 조용히 기본값을 읽게 된다. 그래서 기준값은 전부 인자로 받는다.
// ─────────────────────────────────────────────────────────────────────────────

// 화면 공간 저작 기준. 프로젝트 해상도와 PPU 에서 유도한다(1920x1080 @ 100 → 9.6 x 5.4).
struct ScreenSpaceReference
{
	float HalfWidth     = 9.6f;    // 기준 렉트 반폭(유닛)
	float HalfHeight    = 5.4f;    // 기준 렉트 반높이(유닛)
	float PixelsPerUnit = 100.0f;

	static ScreenSpaceReference FromResolution(float resolutionWidth, float resolutionHeight, float pixelsPerUnit)
	{
		ScreenSpaceReference reference;
		reference.PixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 100.0f;
		reference.HalfWidth  = std::max(resolutionWidth,  1.0f) / reference.PixelsPerUnit * 0.5f;
		reference.HalfHeight = std::max(resolutionHeight, 1.0f) / reference.PixelsPerUnit * 0.5f;
		return reference;
	}
};

// 스케일 모드가 정하는 화면 공간 반높이/반폭(유닛). rectPixel* 은 이 뷰포트 렉트의 픽셀 크기다
// (화면 전체가 아니라 — 스플릿에서 화면 전체 비율을 쓰면 내용이 찌그러진다).
inline void ComputeScreenSpaceExtents(
	EScreenScaleMode mode,
	const ScreenSpaceReference& reference,
	float rectPixelW,
	float rectPixelH,
	float& outHalfWidth,
	float& outHalfHeight)
{
	const float safeRectH = std::max(rectPixelH, 1.0f);
	const float aspect    = std::max(rectPixelW, 1.0f) / safeRectH;

	float halfHeight = reference.HalfHeight;
	switch (mode)
	{
	case EScreenScaleMode::FixedWidth:
		// 기준 폭 유지 → 반높이는 종횡비가 정한다.
		halfHeight = reference.HalfWidth / std::max(aspect, 0.0001f);
		break;
	case EScreenScaleMode::Contain:
		// 기준 렉트가 통째로 들어와야 한다: 반높이 ≥ 기준반높이 **그리고** 반폭 ≥ 기준반폭.
		// 반폭 = 반높이 x aspect 이므로 두 요구를 반높이 하나로 합치면 max 다.
		halfHeight = std::max(reference.HalfHeight, reference.HalfWidth / std::max(aspect, 0.0001f));
		break;
	case EScreenScaleMode::ConstantPixel:
		// 1 유닛 = 항상 PPU 픽셀.
		halfHeight = safeRectH / reference.PixelsPerUnit * 0.5f;
		break;
	case EScreenScaleMode::FixedHeight:
	default:
		halfHeight = reference.HalfHeight;
		break;
	}

	outHalfHeight = std::max(halfHeight, 0.0001f);
	outHalfWidth  = outHalfHeight * aspect;
}

// 화면 공간 뷰 파라미터. 카메라를 전혀 안 본다 — 그게 요점이다.
// 출력 렉트는 호출자가 이미 픽셀로 해석해 둔 값을 그대로 물려준다(같은 뷰포트에 그린다).
inline CanvasViewProjection MakeScreenSpaceViewProjection(
	const CanvasViewProjection& viewportRect,
	EScreenScaleMode mode,
	const ScreenSpaceReference& reference)
{
	CanvasViewProjection view = viewportRect;
	view.PosX = 0.0f;
	view.PosY = 0.0f;
	view.CosR = 1.0f;
	view.SinR = 0.0f;
	ComputeScreenSpaceExtents(mode, reference, view.RectPixelW, view.RectPixelH,
		view.OrthoSizeX, view.OrthoSize);
	return view;
}

// 스크린 픽셀(좌상단 원점)이 이 뷰포트 렉트 안에 있는가.
inline bool CanvasViewProjectionContainsScreen(const CanvasViewProjection& view, float screenX, float screenY)
{
	const float localX = screenX - view.RectPixelX;
	const float localY = screenY - view.RectPixelY;
	return localX >= 0.0f && localX <= view.RectPixelW
		&& localY >= 0.0f && localY <= view.RectPixelH;
}

// 스크린 픽셀(좌상단 원점) → 월드 좌표. 뷰포트 렉트·카메라(위치·회전·오쏘 범위)의 역투영이다.
// Forward2DRenderer 의 뷰 행렬(ndc = R·(world-cam)/half)을 정확히 뒤집는다 — 렌더가 그리는
// 좌표와 피킹 좌표가 일치한다. 패럴랙스 팩터 1(메인 월드) 기준이다: 패럴랙스 레이어(원경·UI)는
// 카메라 위치가 factor 로 스케일되므로 이 결과와 다르다(그 좌표가 필요하면 별도 역투영이 필요).
inline Vector2 CanvasViewProjectionScreenToWorld(const CanvasViewProjection& view, float screenX, float screenY)
{
	// 렉트 로컬 정규화 → NDC. 스크린 y 는 아래로 증가, NDC y 는 위로 증가하므로 y 를 뒤집는다.
	const float u = (screenX - view.RectPixelX) / std::max(view.RectPixelW, 1.0f);
	const float v = (screenY - view.RectPixelY) / std::max(view.RectPixelH, 1.0f);
	const float ndcX = u * 2.0f - 1.0f;
	const float ndcY = 1.0f - v * 2.0f;

	// NDC → 뷰 공간(카메라 중심 원점, 회전 전).
	const float viewX = ndcX * view.OrthoSizeX;
	const float viewY = ndcY * view.OrthoSize;

	// 뷰 → 월드: 카메라 회전 R 을 적용하고 카메라 위치를 더한다.
	//   world = camPos + R * viewOffset,  R = [CosR -SinR; SinR CosR]
	const float dx = view.CosR * viewX - view.SinR * viewY;
	const float dy = view.SinR * viewX + view.CosR * viewY;
	return Vector2(view.PosX + dx, view.PosY + dy);
}
