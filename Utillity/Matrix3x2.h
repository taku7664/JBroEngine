#pragma once

#include "Vector2T.h"

#include <cmath>

// 2D affine transform only. Use a separate Matrix4x4 type for future 3D paths.
struct Matrix3x2
{
	float M11 = 1.0f;
	float M12 = 0.0f;
	float M21 = 0.0f;
	float M22 = 1.0f;
	float Dx = 0.0f;
	float Dy = 0.0f;

	static Matrix3x2 Identity()
	{
		return Matrix3x2();
	}

	static Matrix3x2 Translation(const Vector2<float>& translation)
	{
		Matrix3x2 matrix;
		matrix.Dx = translation.x;
		matrix.Dy = translation.y;
		return matrix;
	}

	static Matrix3x2 Scale(const Vector2<float>& scale)
	{
		Matrix3x2 matrix;
		matrix.M11 = scale.x;
		matrix.M22 = scale.y;
		return matrix;
	}

	static Matrix3x2 Rotation(float radians)
	{
		const float c = std::cos(radians);
		const float s = std::sin(radians);

		Matrix3x2 matrix;
		matrix.M11 = c;
		matrix.M12 = s;
		matrix.M21 = -s;
		matrix.M22 = c;
		return matrix;
	}

	static Matrix3x2 Transform(const Vector2<float>& translation, float rotationRadians, const Vector2<float>& scale)
	{
		return Scale(scale) * Rotation(rotationRadians) * Translation(translation);
	}

	Vector2<float> TransformPoint(const Vector2<float>& point) const
	{
		return Vector2<float>(
			point.x * M11 + point.y * M21 + Dx,
			point.x * M12 + point.y * M22 + Dy
		);
	}

	bool TryInvert(Matrix3x2& out) const
	{
		const float determinant = M11 * M22 - M12 * M21;
		if (std::abs(determinant) <= 0.000001f)
		{
			out = Identity();
			return false;
		}

		const float invDeterminant = 1.0f / determinant;
		out.M11 = M22 * invDeterminant;
		out.M12 = -M12 * invDeterminant;
		out.M21 = -M21 * invDeterminant;
		out.M22 = M11 * invDeterminant;
		out.Dx = (M21 * Dy - Dx * M22) * invDeterminant;
		out.Dy = (Dx * M12 - M11 * Dy) * invDeterminant;
		return true;
	}

	Matrix3x2 operator*(const Matrix3x2& rhs) const
	{
		Matrix3x2 result;
		result.M11 = M11 * rhs.M11 + M12 * rhs.M21;
		result.M12 = M11 * rhs.M12 + M12 * rhs.M22;
		result.M21 = M21 * rhs.M11 + M22 * rhs.M21;
		result.M22 = M21 * rhs.M12 + M22 * rhs.M22;
		result.Dx = Dx * rhs.M11 + Dy * rhs.M21 + rhs.Dx;
		result.Dy = Dx * rhs.M12 + Dy * rhs.M22 + rhs.Dy;
		return result;
	}
};
