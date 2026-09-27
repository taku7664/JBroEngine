#pragma once

#include <JBro/Types/Angle.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Size.h>

#include <cmath>

namespace JBro
{
    struct Vector2  { float x = 0.0f; float y = 0.0f; };

    // 축에 나란한 사각형이다. `min` 은 성분마다의 최솟값, `max` 는 최댓값이다.
    //
    // **비어 있는 사각형과 뒤집힌 사각형은 다르다.** `min` 이 `max` 보다 큰 사각형은 뒤집힌
    // 것이고, 겹침 판정은 그것을 "겹치지 않음" 으로 답한다. 점 하나를 담은 사각형
    // (`min == max`)은 뒤집힌 것이 아니라 넓이가 0 인 것이며, `Contains` 는 그 점을 담았다고
    // 답한다 - 물리의 점 질의가 `Rect{ point, point }` 로 그 자리를 쓴다.
    struct Rect
    {
        Vector2 min;
        Vector2 max;

        constexpr float Width() const noexcept { return max.x - min.x; }
        constexpr float Height() const noexcept { return max.y - min.y; }
        constexpr Size GetSize() const noexcept { return Size(Width(), Height()); }
        constexpr Vector2 Center() const noexcept
        {
            return Vector2{ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
        }

        // 넓이나 높이가 0 이하다. 뒤집힌 사각형도 여기에 걸린다.
        constexpr bool IsEmpty() const noexcept
        {
            return max.x <= min.x || max.y <= min.y;
        }

        // 경계에 놓인 점도 담은 것으로 본다. 맞닿은 두 사각형이 같은 점을 담게 되지만,
        // 담지 않는 것으로 하면 넓이 0 인 사각형이 자기 점조차 담지 못한다.
        constexpr bool Contains(const Vector2& point) const noexcept
        {
            return point.x >= min.x && point.x <= max.x
                && point.y >= min.y && point.y <= max.y;
        }

        constexpr bool Contains(const Rect& other) const noexcept
        {
            return other.min.x >= min.x && other.max.x <= max.x
                && other.min.y >= min.y && other.max.y <= max.y;
        }

        // 맞닿기만 한 것도 겹친 것으로 본다. 넓이 0 인 질의 사각형이 아무것도 못 맞히는 일을
        // 막는다 - 물리의 점 질의가 그 모양이다.
        constexpr bool Intersects(const Rect& other) const noexcept
        {
            return min.x <= other.max.x && max.x >= other.min.x
                && min.y <= other.max.y && max.y >= other.min.y;
        }
    };

    // 왼쪽 아래 구석과 크기로 만든다.
    inline constexpr Rect MakeRect(const Vector2& corner, const Size& size) noexcept
    {
        return Rect{ corner, Vector2{ corner.x + size.width, corner.y + size.height } };
    }

    // 가운데와 크기로 만든다. 카메라·화면 영역이 이 모양이다.
    inline constexpr Rect MakeRectFromCenter(const Vector2& center, const Size& size) noexcept
    {
        const float halfWidth = size.width * 0.5f;
        const float halfHeight = size.height * 0.5f;
        return Rect{
            Vector2{ center.x - halfWidth, center.y - halfHeight },
            Vector2{ center.x + halfWidth, center.y + halfHeight } };
    }

    // 점 하나를 감싸는 사각형이다. 여기에 점을 더해 가며 경계를 넓힌다.
    inline constexpr Rect MakeRectFromPoint(const Vector2& point) noexcept
    {
        return Rect{ point, point };
    }

    // **모서리를 고르는 것은 `fmin`/`fmax` 로 한다.** 삼항 비교와 달리 한쪽이 NaN 이면 성한 쪽을
    // 고른다 - 물리의 경계 상자가 전부터 그렇게 쌓아 왔고, 한 점이 NaN 이라고 상자 전체를
    // NaN 으로 만들면 그 물체가 모든 질의에 걸리거나 아무 질의에도 안 걸린다.
    // `constexpr` 이 아닌 까닭도 이것이다(`std::fmin` 은 C++20 에서 상수식이 아니다).

    // 겹치는 부분이다. **겹치지 않으면 뒤집힌 사각형이 나온다** - 받은 쪽에서 `IsEmpty` 로
    // 확인한다. 여기서 임의의 빈 사각형을 만들어 돌려주면 그 자리가 어디인지 뜻이 없어진다.
    inline Rect IntersectRect(const Rect& left, const Rect& right) noexcept
    {
        return Rect{
            Vector2{ std::fmax(left.min.x, right.min.x), std::fmax(left.min.y, right.min.y) },
            Vector2{ std::fmin(left.max.x, right.max.x), std::fmin(left.max.y, right.max.y) } };
    }

    // 둘을 다 담는 가장 작은 사각형이다.
    inline Rect UnionRect(const Rect& left, const Rect& right) noexcept
    {
        return Rect{
            Vector2{ std::fmin(left.min.x, right.min.x), std::fmin(left.min.y, right.min.y) },
            Vector2{ std::fmax(left.max.x, right.max.x), std::fmax(left.max.y, right.max.y) } };
    }

    // 점 하나를 더 담도록 넓힌다.
    inline Rect UnionRect(const Rect& rect, const Vector2& point) noexcept
    {
        return Rect{
            Vector2{ std::fmin(rect.min.x, point.x), std::fmin(rect.min.y, point.y) },
            Vector2{ std::fmax(rect.max.x, point.x), std::fmax(rect.max.y, point.y) } };
    }

    inline constexpr Rect OffsetRect(const Rect& rect, const Vector2& offset) noexcept
    {
        return Rect{
            Vector2{ rect.min.x + offset.x, rect.min.y + offset.y },
            Vector2{ rect.max.x + offset.x, rect.max.y + offset.y } };
    }

    // 네 방향으로 넓힌다. 음수를 넘기면 줄어들고, 많이 줄이면 뒤집힌다.
    inline constexpr Rect ExpandRect(const Rect& rect, float margin) noexcept
    {
        return Rect{
            Vector2{ rect.min.x - margin, rect.min.y - margin },
            Vector2{ rect.max.x + margin, rect.max.y + margin } };
    }

    struct Matrix3x2
    {
        float m11 = 1.0f; float m12 = 0.0f;
        float m21 = 0.0f; float m22 = 1.0f;
        float m31 = 0.0f; float m32 = 0.0f;
    };

    inline Matrix3x2 MakeTransformMatrix2D(
        const Vector2& position,
        Radian rotation,
        const Vector2& scale)
    {
        const float cosine = std::cos(rotation.Get());
        const float sine = std::sin(rotation.Get());

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
