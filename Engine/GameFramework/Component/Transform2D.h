#pragma once

#include "Utillity/Math/Matrix3x2.h"
#include "Utillity/Math/Vector2T.h"

struct Transform2D
{
	Vector2 Position = Vector2(0.0f, 0.0f);
	Radian  RotationRadians = 0.0f;
	Vector2 Scale = Vector2(1.0f, 1.0f);

	Matrix3x2 ToMatrix3x2() const
	{
		return Matrix3x2::Transform(Position, RotationRadians, Scale);
	}
};

