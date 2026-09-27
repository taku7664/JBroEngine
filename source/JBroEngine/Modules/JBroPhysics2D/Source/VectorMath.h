#pragma once

#include <JBro/Types/Math2D.h>

#include <cmath>

// 커널 소스끼리 쓰는 벡터 연산이다. 공개 헤더가 아니다 - 스크립트와 엔진의 다른 모듈은 Vector2 를 값으로만 다룬다.
namespace JBro::Physics2D::Internal
{
    inline Vector2 Add(Vector2 a, Vector2 b)
    {
        return { a.x + b.x, a.y + b.y };
    }

    inline Vector2 Subtract(Vector2 a, Vector2 b)
    {
        return { a.x - b.x, a.y - b.y };
    }

    inline Vector2 Scale(Vector2 a, float k)
    {
        return { a.x * k, a.y * k };
    }

    inline float Dot(Vector2 a, Vector2 b)
    {
        return a.x * b.x + a.y * b.y;
    }

    inline float Cross(Vector2 a, Vector2 b)
    {
        return a.x * b.y - a.y * b.x;
    }

    // 각속도 w 와 팔 r 의 곱(w × r). 회전하는 점의 속도다.
    inline Vector2 Cross(float w, Vector2 r)
    {
        return { -w * r.y, w * r.x };
    }

    // 성분끼리 곱한다. 축마다 다른 역질량(축 고정)에 쓴다.
    inline Vector2 Multiply(Vector2 a, Vector2 b)
    {
        return { a.x * b.x, a.y * b.y };
    }

    inline float LengthSquared(Vector2 a)
    {
        return a.x * a.x + a.y * a.y;
    }

    inline float Length(Vector2 a)
    {
        return std::sqrt(LengthSquared(a));
    }
}
