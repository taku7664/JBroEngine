#pragma once

#include "GameFramework/Component/Component.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>

enum class ELight2DType
{
	Directional,
	Point,
	Spot
};

class Light2D final : public CComponent
{
	JBRO_COMPONENT(Light2D)
public:
	ELight2DType Type = ELight2DType::Point;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float Intensity = 1.0f;
	float Range = 5.0f;
	float InnerAngleRadians = 0.5f;
	float OuterAngleRadians = 1.0f;
	std::uint32_t LayerMask = 0xffffffffu;
	bool CastShadows = false;
	// Directional 그림자 도달 길이(월드 유닛). 태양 고도가 낮을수록 길게 — 유한 그림자를 만든다.
	float ShadowLength = 6.0f;
	// 소프트 쉐도우 강도 0~1(0=하드 경계, 1=최대 블러). 그림자 겉 경계를 부드럽게 한다.
	float ShadowSoftness = 0.5f;
};
