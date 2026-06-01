#pragma once

#include <cstdint>
#include "Utillity/Math/Layout2D.h"

enum class ECameraProjectionMode2D
{
	Orthographic,
	PerspectiveReady
};

struct Camera2D
{
	bool IsEnabled = true;
	ECameraProjectionMode2D ProjectionMode = ECameraProjectionMode2D::Orthographic;
	float OrthographicSize = 10.0f;
	float PerspectiveFovDegrees = 60.0f;
	float NearClip = -1.0f;
	float FarClip = 1.0f;

	// ──────────────────────────────────────────────────────────────────
	// 카메라 뷰포트 레이아웃 (Layout2D: Normalized × 해상도 + Pixel)
	//   Position : 게임 렌더 타겟 상의 뷰포트 좌상단 오프셋
	//              기본값 (0,0)(0,0) → 좌상단 원점
	//   Size     : 뷰포트의 픽셀 크기
	//              기본값 (1,1)(0,0) → 전체 해상도
	// ──────────────────────────────────────────────────────────────────
	Layout2D Position = { Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f) };
	Layout2D Size     = { Vector2(1.0f, 1.0f), Vector2(0.0f, 0.0f) };

	float ClearColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	std::uint32_t LayerMask = 0xffffffffu;
	std::int32_t Priority = 0;
	bool IsMainCamera = false;
};

