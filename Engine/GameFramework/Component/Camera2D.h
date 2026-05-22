#pragma once

#include <cstdint>

enum class ECameraProjectionMode2D
{
	Orthographic,
	PerspectiveReady
};

struct Camera2D
{
	ECameraProjectionMode2D ProjectionMode = ECameraProjectionMode2D::Orthographic;
	float OrthographicSize = 10.0f;
	float PerspectiveFovDegrees = 60.0f;
	float NearClip = -1.0f;
	float FarClip = 1.0f;
	float ViewportX = 0.0f;
	float ViewportY = 0.0f;
	float ViewportWidth = 1.0f;
	float ViewportHeight = 1.0f;
	float ClearColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	std::uint32_t LayerMask = 0xffffffffu;
	std::int32_t Priority = 0;
	bool IsMainCamera = false;
};

