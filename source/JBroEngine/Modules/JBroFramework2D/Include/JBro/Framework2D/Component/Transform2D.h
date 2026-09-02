#pragma once

#include <JBro/Framework2D/Math2D.h>

namespace JBro::Engine
{
    struct Transform2DComponent
    {
        Vec2 position;
        float rotation = 0.0f;
        Vec2 scale{ 1.0f, 1.0f };
    };

    struct WorldTransform2DComponent
    {
        Matrix3x2 matrix;
        Vec2 position;
        float rotation = 0.0f;
        Vec2 scale{ 1.0f, 1.0f };
        bool dirty = true;
    };
}
