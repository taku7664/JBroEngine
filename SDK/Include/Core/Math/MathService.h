#pragma once

#include "Utillity/Math/Matrix3x2.h"
#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Math/Vector2T.h"

class CMathService final : public EnableSafeFromThis<CMathService>
{
public:
	float DegreesToRadians(float degrees) const;
	float RadiansToDegrees(float radians) const;
	float Clamp(float value, float minValue, float maxValue) const;
	float Lerp(float a, float b, float t) const;
	Matrix3x2 Compose2D(const Vector2& position, float rotationRadians, const Vector2& scale) const;
};
