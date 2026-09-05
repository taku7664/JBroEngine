#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>

namespace JBro
{
    class GameObject;
}

namespace JBro::Component
{
    enum class BodyType2D      : std::uint8_t { Static, Kinematic, Dynamic };
    enum class ColliderShape2D : std::uint8_t { Box, Circle, Capsule, Polygon };

    class Rigidbody2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        BodyType2D bodyType = BodyType2D::Dynamic;
        Vec2       linearVelocity;
        float      angularVelocity = 0.0f;
        float      mass          = 1.0f;
        float      gravityScale  = 1.0f;
        float      linearDamping = 0.0f;
        bool       fixedRotation = false;
    };

    class Collider2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        ColliderShape2D shape = ColliderShape2D::Box;
        Vec2 offset;
        Vec2 size{ 1.0f, 1.0f };
        float radius = 0.5f;
        bool  isTrigger = false;
    };
}

namespace JBro
{
    struct Collision2D
    {
        GameObject* other = nullptr;
        Component::BodyType2D bodyType = Component::BodyType2D::Dynamic;
        Vec2 point;
        Vec2 normal;
    };
}
