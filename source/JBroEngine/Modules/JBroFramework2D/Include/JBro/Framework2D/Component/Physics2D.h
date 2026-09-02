#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2D.h>

#include <cstdint>

namespace JBro::Engine
{
    enum class BodyType2D : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    enum class ColliderShape2D : std::uint8_t
    {
        Box,
        Circle,
        Capsule,
        Polygon
    };

    struct Rigidbody2DComponent
    {
        BodyType2D bodyType = BodyType2D::Dynamic;
        Vec2 linearVelocity;
        float angularVelocity = 0.0f;
        float mass = 1.0f;
        float gravityScale = 1.0f;
        float linearDamping = 0.0f;
        bool fixedRotation = false;
    };

    struct Collider2DComponent
    {
        ColliderShape2D shape = ColliderShape2D::Box;
        Vec2 offset;
        Vec2 size{ 1.0f, 1.0f };
        float radius = 0.5f;
        float density = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;
        bool trigger = false;
    };

    struct Collision2D
    {
        Entity self;
        Entity other;
        Vec2 point;
        Vec2 normal;
        float penetration = 0.0f;
    };
}
