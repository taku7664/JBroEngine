#pragma once

#include <cmath>

// 3D 수학 값 타입과 hot-path 용 inline 함수다. 리플렉션은 `Math3DReflection.h` 에 따로 있다 -
// 이 헤더는 매 프레임 경로에 있어 리플렉션 기계를 물고 가면 안 된다(2D 의 `Math2D.h` 와 같다).
//
// 규약(framework3d-plan §2.2, `[가정]`): 오른손 좌표, 카메라는 -Z 를 본다, 사원수는 (x, y, z, w) 이고
// w 가 실수부다. 행렬은 `JBroGraphics` 의 `Matrix4x4` 가 소유하므로 여기에는 없다 - 행렬을 만드는
// 함수는 `JBroFramework3DSystem/Math3DMatrix.h` 에 있다.
namespace JBro
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Quaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    inline Vec3 Add(const Vec3& left, const Vec3& right)
    {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }

    inline Vec3 Subtract(const Vec3& left, const Vec3& right)
    {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    inline Vec3 Scale(const Vec3& value, float factor)
    {
        return {value.x * factor, value.y * factor, value.z * factor};
    }

    // 성분마다 곱한다. 스케일을 겹칠 때 쓴다.
    inline Vec3 Multiply(const Vec3& left, const Vec3& right)
    {
        return {left.x * right.x, left.y * right.y, left.z * right.z};
    }

    inline float Dot(const Vec3& left, const Vec3& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    inline Vec3 Cross(const Vec3& left, const Vec3& right)
    {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
    }

    inline float Length(const Vec3& value)
    {
        return std::sqrt(Dot(value, value));
    }

    // 길이가 0 이면 그대로 0 벡터다. 나눗셈으로 NaN 을 만들지 않는다.
    inline Vec3 Normalize(const Vec3& value)
    {
        const float length = Length(value);
        if (length <= 0.0f)
        {
            return {};
        }
        return Scale(value, 1.0f / length);
    }

    inline Quaternion Normalize(const Quaternion& value)
    {
        const float length = std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
        if (length <= 0.0f)
        {
            return {};
        }
        const float inverse = 1.0f / length;
        return {value.x * inverse, value.y * inverse, value.z * inverse, value.w * inverse};
    }

    // 단위 사원수의 역이다. `left` 뒤에 `right` 를 적용하는 곱은 `Multiply(right, left)` 가 아니라
    // 벡터 회전 규약(`Rotate(q, v)` = q v q*)을 따라 `Multiply(parent, child)` 가 "부모 회전 뒤 자식
    // 회전" 이 되도록 정의한다.
    inline Quaternion Conjugate(const Quaternion& value)
    {
        return {-value.x, -value.y, -value.z, value.w};
    }

    // 해밀턴 곱. `Rotate(Multiply(a, b), v) == Rotate(a, Rotate(b, v))`.
    inline Quaternion Multiply(const Quaternion& a, const Quaternion& b)
    {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    // q v q*. 단위 사원수를 전제한다.
    inline Vec3 Rotate(const Quaternion& rotation, const Vec3& value)
    {
        const Vec3 axis{rotation.x, rotation.y, rotation.z};
        const Vec3 crossed = Cross(axis, value);
        const Vec3 crossedTwice = Cross(axis, crossed);
        return Add(value, Add(Scale(crossed, 2.0f * rotation.w), Scale(crossedTwice, 2.0f)));
    }

    // 축은 정규화한다. 각은 라디안이고 오른손 규칙이다.
    inline Quaternion FromAxisAngle(const Vec3& axis, float radians)
    {
        const Vec3 unit = Normalize(axis);
        const float half = radians * 0.5f;
        const float sine = std::sin(half);
        return {unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
    }

    // 오일러각(라디안). Z(roll) → X(pitch) → Y(yaw) 순으로 적용한다 - Unity 와 같은 차례라 인스펙터가
    // 사람에게 보여 주는 값으로 쓴다. `[가정]`
    inline Quaternion FromEuler(const Vec3& radians)
    {
        const Quaternion yaw = FromAxisAngle({0.0f, 1.0f, 0.0f}, radians.y);
        const Quaternion pitch = FromAxisAngle({1.0f, 0.0f, 0.0f}, radians.x);
        const Quaternion roll = FromAxisAngle({0.0f, 0.0f, 1.0f}, radians.z);
        return Normalize(Multiply(yaw, Multiply(pitch, roll)));
    }

    inline bool NearlyEqual(const Vec3& left, const Vec3& right, float tolerance = 0.0001f)
    {
        return std::fabs(left.x - right.x) <= tolerance
            && std::fabs(left.y - right.y) <= tolerance
            && std::fabs(left.z - right.z) <= tolerance;
    }
}
