#pragma once

namespace JBro::Engine
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct Rect
    {
        Vec2 min;
        Vec2 max;
    };

    struct Matrix3x2
    {
        float m11 = 1.0f;
        float m12 = 0.0f;
        float m21 = 0.0f;
        float m22 = 1.0f;
        float m31 = 0.0f;
        float m32 = 0.0f;
    };
}
