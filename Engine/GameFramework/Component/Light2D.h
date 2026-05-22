#pragma once

#include "Utillity/Vector2T.h"

#include <cstdint>

enum class ELight2DType
{
	Directional,
	Point,
	SpotReady
};

struct Light2D
{
	ELight2DType Type = ELight2DType::Point;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float Intensity = 1.0f;
	float Range = 5.0f;
	float InnerAngleRadians = 0.5f;
	float OuterAngleRadians = 1.0f;
	std::uint32_t LayerMask = 0xffffffffu;
	bool CastShadows = false;
};
