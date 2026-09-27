#pragma once

#include <JBro/Types/Color.h>

#include <cmath>

namespace JBro
{
    struct Vec2  { float x = 0.0f; float y = 0.0f; };
    struct Rect  { Vec2 min; Vec2 max; };
    struct Matrix3x2
    {
        float m11 = 1.0f; float m12 = 0.0f;
        float m21 = 0.0f; float m22 = 1.0f;
        float m31 = 0.0f; float m32 = 0.0f;
    };

    inline Matrix3x2 MakeTransformMatrix2D(
        const Vec2& position,
        float rotation,
        const Vec2& scale)
    {
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);

        Matrix3x2 result;
        result.m11 = cosine * scale.x;
        result.m12 = sine * scale.x;
        result.m21 = -sine * scale.y;
        result.m22 = cosine * scale.y;
        result.m31 = position.x;
        result.m32 = position.y;
        return result;
    }

    inline Matrix3x2 MultiplyMatrix3x2(
        const Matrix3x2& left,
        const Matrix3x2& right)
    {
        Matrix3x2 result;
        result.m11 = left.m11 * right.m11 + left.m12 * right.m21;
        result.m12 = left.m11 * right.m12 + left.m12 * right.m22;
        result.m21 = left.m21 * right.m11 + left.m22 * right.m21;
        result.m22 = left.m21 * right.m12 + left.m22 * right.m22;
        result.m31 = left.m31 * right.m11 + left.m32 * right.m21 + right.m31;
        result.m32 = left.m31 * right.m12 + left.m32 * right.m22 + right.m32;
        return result;
    }
}
