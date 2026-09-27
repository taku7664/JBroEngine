#pragma once

#include <JBro/Types/Angle.h>

#include <cmath>

// 3D 수학 값 타입과 hot-path 용 inline 함수다. 리플렉션은 `Math3DReflection.h` 에 따로 있다 -
// 이 헤더는 매 프레임 경로에 있어 리플렉션 기계를 물고 가면 안 된다(2D 의 `Math2D.h` 와 같다).
//
// 규약(framework3d-plan §2.2, `[가정]`): 오른손 좌표, 카메라는 -Z 를 본다, 사원수는 (x, y, z, w) 이고
// w 가 실수부다. `Matrix4x4` 는 차원 무관한 배치 규약을 가지므로 같은 Core 의
// `JBro/Types/Matrix4x4.h` 에 따로 있고, 좌표계를 전제해 그 행렬을 만드는 함수는
// `JBroFramework3DSystem/Math3DMatrix.h` 에 있다.
namespace JBro
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // 성분 넷짜리 벡터다(D-250). **`Quaternion` 과 배치는 같지만 뜻이 다르다** - 사원수는 회전이고
    // `w` 가 실수부라 기본값이 1 이다. 이쪽은 그냥 숫자 넷이라 전부 0 에서 시작한다. 둘을 한 타입으로
    // 겸하면 "기본값이 무엇이냐" 에서 반드시 틀린다.
    //
    // 쓰는 자리는 동차 좌표(`Matrix4x4` 와 곱하는 점·방향), 셰이더 상수, 평면의 방정식이다.
    // 색은 `Color` 가 따로 들고 있으니 이것으로 대신하지 않는다 - 색은 감마와 알파 규약이 붙는다.
    //
    // 2D·3D 어느 쪽 것도 아니지만 `Matrix4x4` 와 짝이라 여기 둔다.
    struct Vector4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Quaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    inline Vector3 Add(const Vector3& left, const Vector3& right)
    {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }

    inline Vector3 Subtract(const Vector3& left, const Vector3& right)
    {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    inline Vector3 Scale(const Vector3& value, float factor)
    {
        return {value.x * factor, value.y * factor, value.z * factor};
    }

    // 성분마다 곱한다. 스케일을 겹칠 때 쓴다.
    inline Vector3 Multiply(const Vector3& left, const Vector3& right)
    {
        return {left.x * right.x, left.y * right.y, left.z * right.z};
    }

    inline float Dot(const Vector3& left, const Vector3& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    inline Vector3 Cross(const Vector3& left, const Vector3& right)
    {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
    }

    inline float Length(const Vector3& value)
    {
        return std::sqrt(Dot(value, value));
    }

    // 길이가 0 이면 그대로 0 벡터다. 나눗셈으로 NaN 을 만들지 않는다.
    inline Vector3 Normalize(const Vector3& value)
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
    inline Vector3 Rotate(const Quaternion& rotation, const Vector3& value)
    {
        const Vector3 axis{rotation.x, rotation.y, rotation.z};
        const Vector3 crossed = Cross(axis, value);
        const Vector3 crossedTwice = Cross(axis, crossed);
        return Add(value, Add(Scale(crossed, 2.0f * rotation.w), Scale(crossedTwice, 2.0f)));
    }

    // 축은 정규화한다. 오른손 규칙이다.
    inline Quaternion FromAxisAngle(const Vector3& axis, Radian angle)
    {
        const Vector3 unit = Normalize(axis);
        const float half = angle.Get() * 0.5f;
        const float sine = std::sin(half);
        return {unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
    }

    // 오일러각(라디안). Z(roll) → X(pitch) → Y(yaw) 순으로 적용한다 - Unity 와 같은 차례라 인스펙터가
    // 사람에게 보여 주는 값으로 쓴다. `[가정]`
    inline Quaternion FromEuler(const Vector3& radians)
    {
        const Quaternion yaw = FromAxisAngle({0.0f, 1.0f, 0.0f}, radians.y);
        const Quaternion pitch = FromAxisAngle({1.0f, 0.0f, 0.0f}, radians.x);
        const Quaternion roll = FromAxisAngle({0.0f, 0.0f, 1.0f}, radians.z);
        return Normalize(Multiply(yaw, Multiply(pitch, roll)));
    }

    inline bool NearlyEqual(const Vector3& left, const Vector3& right, float tolerance = 0.0001f)
    {
        return std::fabs(left.x - right.x) <= tolerance
            && std::fabs(left.y - right.y) <= tolerance
            && std::fabs(left.z - right.z) <= tolerance;
    }

    // ── Vector4 ─────────────────────────────────────────────────────────────
    // 외적은 두지 않는다 - 4 차원에서 벡터 둘의 외적은 뜻이 없다.

    inline Vector4 Add(const Vector4& left, const Vector4& right)
    {
        return {left.x + right.x, left.y + right.y, left.z + right.z, left.w + right.w};
    }

    inline Vector4 Subtract(const Vector4& left, const Vector4& right)
    {
        return {left.x - right.x, left.y - right.y, left.z - right.z, left.w - right.w};
    }

    inline Vector4 Scale(const Vector4& value, float factor)
    {
        return {value.x * factor, value.y * factor, value.z * factor, value.w * factor};
    }

    inline Vector4 Multiply(const Vector4& left, const Vector4& right)
    {
        return {left.x * right.x, left.y * right.y, left.z * right.z, left.w * right.w};
    }

    inline float Dot(const Vector4& left, const Vector4& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
    }

    inline float Length(const Vector4& value)
    {
        return std::sqrt(Dot(value, value));
    }

    inline Vector4 Normalize(const Vector4& value)
    {
        const float length = Length(value);
        if (length <= 0.0001f)
        {
            return {};
        }
        return Scale(value, 1.0f / length);
    }

    // 점은 `w = 1`, 방향은 `w = 0` 이다. 동차 좌표에서 이 둘을 가르는 것이 `w` 다 -
    // 평행이동이 붙는 점과 안 붙는 방향이 갈린다.
    inline Vector4 MakePoint(const Vector3& value)
    {
        return {value.x, value.y, value.z, 1.0f};
    }

    inline Vector4 MakeDirection(const Vector3& value)
    {
        return {value.x, value.y, value.z, 0.0f};
    }

    // **`w` 로 나눈다**(원근 나눗셈). `w` 가 0 이면 무한히 먼 점이라 나눌 수 없으므로
    // 나누지 않고 앞의 셋을 그대로 준다 - 여기서 무한을 만들면 그 값이 뒤로 번진다.
    inline Vector3 ToVector3(const Vector4& value)
    {
        if (value.w == 0.0f || value.w == 1.0f)
        {
            return {value.x, value.y, value.z};
        }
        const float inverse = 1.0f / value.w;
        return {value.x * inverse, value.y * inverse, value.z * inverse};
    }

    inline bool NearlyEqual(const Vector4& left, const Vector4& right, float tolerance = 0.0001f)
    {
        return std::fabs(left.x - right.x) <= tolerance
            && std::fabs(left.y - right.y) <= tolerance
            && std::fabs(left.z - right.z) <= tolerance
            && std::fabs(left.w - right.w) <= tolerance;
    }
}
