#pragma once

#include <cstdint>

// 4x4 행렬 값 타입이다. 배치는 `values[row * 4 + column]` 이고 열 벡터를 쓴다(`x' = row0 · v`).
// 이 배치는 GPU 상수 버퍼로 그대로 올라가므로 렌더러·셰이더와의 계약이다 - 바꾸면 백엔드 셋이
// 함께 바뀐다.
//
// 좌표계와 깊이 범위(오른손 좌표, 카메라는 -Z 를 본다, 깊이 0..1)를 전제하는 함수 - 투영·뷰·회전
// 행렬을 만드는 것들 - 은 여기가 아니라 `JBroFramework3DSystem/Math3DMatrix.h` 에 있다.
// 여기에는 배치만 알면 되는 순수 연산을 둔다.
namespace JBro
{
    struct Matrix4x4
    {
        float values[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
    };

    inline Matrix4x4 MultiplyMatrix4x4(const Matrix4x4& left, const Matrix4x4& right)
    {
        Matrix4x4 result;
        for (std::uint32_t row = 0; row < 4; ++row)
        {
            for (std::uint32_t column = 0; column < 4; ++column)
            {
                float value = 0.0f;
                for (std::uint32_t element = 0; element < 4; ++element)
                {
                    value += left.values[row * 4 + element] * right.values[element * 4 + column];
                }
                result.values[row * 4 + column] = value;
            }
        }
        return result;
    }
}
