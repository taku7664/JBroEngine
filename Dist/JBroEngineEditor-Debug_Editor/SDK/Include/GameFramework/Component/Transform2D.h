#pragma once

#include "Utillity/Matrix3x2.h"
#include "Utillity/Vector2T.h"

struct Transform2D
{
	Vector2<float> Position = Vector2<float>(0.0f, 0.0f);
	float RotationRadians = 0.0f;
	Vector2<float> Scale = Vector2<float>(1.0f, 1.0f);

	Matrix3x2 ToMatrix3x2() const
	{
		return Matrix3x2::Transform(Position, RotationRadians, Scale);
	}
};

