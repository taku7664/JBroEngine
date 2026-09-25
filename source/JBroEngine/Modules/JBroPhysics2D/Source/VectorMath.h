#pragma once

#include <JBro/Framework2D/Math2D.h>

#include <cmath>

// 커널 소스끼리 쓰는 벡터 연산이다. 공개 헤더가 아니다 - 스크립트와 엔진의 다른 모듈은 Vec2 를 값으로만 다룬다.
namespace JBro::Physics2D::Internal
{
    inline Vec2 Add(Vec2 a, Vec2 b)
    {
        return { a.x + b.x, a.y + b.y };
    }

    inline Vec2 Subtract(Vec2 a, Vec2 b)
    {
        return { a.x - b.x, a.y - b.y };
    }

    inline Vec2 Scale(Vec2 a, float k)
    {
        return { a.x * k, a.y * k };
    }

    inline float Dot(Vec2 a, Vec2 b)
    {
        return a.x * b.x + a.y * b.y;
    }

    inline float Cross(Vec2 a, Vec2 b)
    {
        return a.x * b.y - a.y * b.x;
    }

    // 각속도 w 와 팔 r 의 곱(w × r). 회전하는 점의 속도다.
    inline Vec2 Cross(float w, Vec2 r)
    {
        return { -w * r.y, w * r.x };
    }

    inline float LengthSquared(Vec2 a)
    {
        return a.x * a.x + a.y * a.y;
    }

    inline float Length(Vec2 a)
    {
        return std::sqrt(LengthSquared(a));
    }
}
