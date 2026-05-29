#include "pch.h"
#include "MathService.h"

float CMathService::DegreesToRadians(float degrees) const
{
	return degrees * 0.017453292519943295f;
}

float CMathService::RadiansToDegrees(float radians) const
{
	return radians * 57.29577951308232f;
}

float CMathService::Clamp(float value, float minValue, float maxValue) const
{
	return std::clamp(value, minValue, maxValue);
}

float CMathService::Lerp(float a, float b, float t) const
{
	return a + (b - a) * t;
}

Matrix3x2 CMathService::Compose2D(const Vector2<float>& position, float rotationRadians, const Vector2<float>& scale) const
{
	return Matrix3x2::Transform(position, rotationRadians, scale);
}
