#pragma once

#include <JBro/Types/Math3D.h>
#include <JBro/Types/Matrix4x4.h>

#include <cmath>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

// `Matrix4x4`(JBroCore 소유)를 3D 값에서 만드는 함수다. 컴포넌트 라이브러리에는 두지 않는다 -
// 이 함수들은 좌표계와 깊이 범위를 전제하고, 그 규약은 시스템 단계의 것이다(framework3d-plan §2.2).
//
// 규약은 렌더러와 같다: `values[row * 4 + col]`, 열 벡터(`x' = row0 · v`), 오른손 좌표, 카메라는
// -Z 를 보고, 깊이는 0(근평면)..1(원평면)이다.
namespace JBro
{
    // 회전(단위 사원수) 3x3 을 4x4 의 왼쪽 위에 놓는다.
    inline Matrix4x4 MakeRotationMatrix(const Quaternion& q)
    {
        const Float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const Float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const Float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        return {{
            1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),        2.0f * (xz + wy),        0.0f,
            2.0f * (xy + wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx),        0.0f,
            2.0f * (xz - wy),        2.0f * (yz + wx),        1.0f - 2.0f * (xx + yy), 0.0f,
            0.0f,                    0.0f,                    0.0f,                    1.0f}};
    }

    // T * R * S. 열마다 스케일이 곱해지고 평행이동은 마지막 열이다.
    inline Matrix4x4 MakeTransformMatrix3D(
        const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        Matrix4x4 result = MakeRotationMatrix(rotation);
        for (UInt32 row = 0; row < 3; ++row)
        {
            result.values[row * 4 + 0] *= scale.x;
            result.values[row * 4 + 1] *= scale.y;
            result.values[row * 4 + 2] *= scale.z;
        }
        result.values[3] = position.x;
        result.values[7] = position.y;
        result.values[11] = position.z;
        return result;
    }

    // 카메라 TRS 의 역이다. 스케일은 무시한다 - 카메라를 키우면 세상이 줄어 보이는 것이 아니라
    // 아무 일도 없어야 한다. view = R(q*) * T(-p).
    inline Matrix4x4 MakeViewMatrix(const Vector3& position, const Quaternion& rotation)
    {
        Matrix4x4 result = MakeRotationMatrix(Conjugate(Normalize(rotation)));
        const Vector3 translated = Rotate(Conjugate(Normalize(rotation)), Scale(position, -1.0f));
        result.values[3] = translated.x;
        result.values[7] = translated.y;
        result.values[11] = translated.z;
        return result;
    }

    // 원근 투영. 세로 시야각은 라디안, 깊이 0..1, -Z 를 본다. 값이 말이 안 되면 거짓이다.
    inline Bool MakePerspectiveMatrix(
        Float verticalFieldOfViewRadians, Float aspect, Float nearPlane, Float farPlane, Matrix4x4& out)
    {
        if (false == std::isfinite(verticalFieldOfViewRadians) || verticalFieldOfViewRadians <= 0.0f
            || verticalFieldOfViewRadians >= 3.14159265f
            || false == std::isfinite(aspect) || aspect <= 0.0f
            || false == std::isfinite(nearPlane) || false == std::isfinite(farPlane)
            || nearPlane <= 0.0f || nearPlane >= farPlane)
        {
            return false;
        }
        const Float focal = 1.0f / std::tan(verticalFieldOfViewRadians * 0.5f);
        const Float depth = nearPlane - farPlane;
        out = {{
            focal / aspect, 0.0f,  0.0f,             0.0f,
            0.0f,           focal, 0.0f,             0.0f,
            0.0f,           0.0f,  farPlane / depth, nearPlane * farPlane / depth,
            0.0f,           0.0f,  -1.0f,            0.0f}};
        return true;
    }

    // 직교 투영. `halfHeight` 는 세로 절반 크기(월드 단위). 깊이는 -Z 방향으로 near..far 를 0..1 로.
    inline Bool MakeOrthographicMatrix(
        Float halfHeight, Float aspect, Float nearPlane, Float farPlane, Matrix4x4& out)
    {
        if (false == std::isfinite(halfHeight) || halfHeight <= 0.0f
            || false == std::isfinite(aspect) || aspect <= 0.0f
            || false == std::isfinite(nearPlane) || false == std::isfinite(farPlane)
            || nearPlane >= farPlane)
        {
            return false;
        }
        const Float halfWidth = halfHeight * aspect;
        const Float depth = farPlane - nearPlane;
        out = {{
            1.0f / halfWidth, 0.0f,              0.0f,          0.0f,
            0.0f,             1.0f / halfHeight, 0.0f,          0.0f,
            0.0f,             0.0f,              -1.0f / depth, -nearPlane / depth,
            0.0f,             0.0f,              0.0f,          1.0f}};
        return true;
    }

    // 점 하나를 행렬로 옮긴다(w 나눗셈 포함). 테스트와 기즈모가 쓴다.
    inline Bool TransformPoint(const Matrix4x4& matrix, const Vector3& point, Vector3& out)
    {
        const Float* m = matrix.values;
        const Float x = m[0] * point.x + m[1] * point.y + m[2] * point.z + m[3];
        const Float y = m[4] * point.x + m[5] * point.y + m[6] * point.z + m[7];
        const Float z = m[8] * point.x + m[9] * point.y + m[10] * point.z + m[11];
        const Float w = m[12] * point.x + m[13] * point.y + m[14] * point.z + m[15];
        if (w == 0.0f || false == std::isfinite(w))
        {
            return false;
        }
        out = {x / w, y / w, z / w};
        return true;
    }
}
