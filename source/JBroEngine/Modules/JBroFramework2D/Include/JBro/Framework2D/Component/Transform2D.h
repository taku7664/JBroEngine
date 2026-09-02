#pragma once

#include <JBro/Framework2D/Math2D.h>

namespace JBro::Component
{
    // §10.1: 컴포넌트는 JBro::Component 네임스페이스, 이름에서 Component 접미 제거.
    struct Transform2D
    {
        Vec2  position;
        float rotation = 0.0f;
        Vec2  scale{ 1.0f, 1.0f };
    };

    struct WorldTransform2D
    {
        Matrix3x2 matrix;
        Vec2      position;
        float     rotation = 0.0f;
        Vec2      scale{ 1.0f, 1.0f };
        bool      dirty = true;
    };
}
