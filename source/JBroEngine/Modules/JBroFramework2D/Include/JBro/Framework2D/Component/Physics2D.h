#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>

#include <cstdint>

namespace JBro::Component
{
    enum class BodyType2D      : std::uint8_t { Static, Kinematic, Dynamic };
    enum class ColliderShape2D : std::uint8_t { Box, Circle, Capsule, Polygon };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::BodyType2D, "Component::BodyType2D",
        { Component::BodyType2D::Static,    "Static" },
        { Component::BodyType2D::Kinematic, "Kinematic" },
        { Component::BodyType2D::Dynamic,   "Dynamic" });

    JBRO_DEFINE_ENUM_TYPE(Component::ColliderShape2D, "Component::ColliderShape2D",
        { Component::ColliderShape2D::Box,     "Box" },
        { Component::ColliderShape2D::Circle,  "Circle" },
        { Component::ColliderShape2D::Capsule, "Capsule" },
        { Component::ColliderShape2D::Polygon, "Polygon" });
}

namespace JBro::Component
{
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

        JBRO_REFLECT_BODY(Rigidbody2D)

        JBRO_FIELD(BodyType2D, bodyType) = BodyType2D::Dynamic;
        // 속도는 시뮬레이션이 매 프레임 다시 쓴다. 저장하면 씬을 열 때마다
        // 물체가 저장된 순간의 속도로 튀어 나간다.
        JBRO_FIELD(Vec2,  linearVelocity,  NoSerialize());
        JBRO_FIELD(float, angularVelocity, NoSerialize()) = 0.0f;
        JBRO_FIELD(float, mass,          Range(0, 1000)) = 1.0f;
        JBRO_FIELD(float, gravityScale)  = 1.0f;
        JBRO_FIELD(float, linearDamping) = 0.0f;
        JBRO_FIELD(bool,  fixedRotation) = false;
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

        JBRO_REFLECT_BODY(Collider2D)

        JBRO_FIELD(ColliderShape2D, shape) = ColliderShape2D::Box;
        JBRO_FIELD(Vec2,  offset);
        JBRO_FIELD(Vec2,  size) { 1.0f, 1.0f };
        JBRO_FIELD(float, radius) = 0.5f;
        JBRO_FIELD(bool,  isTrigger) = false;
    };
}

namespace JBro
{
    struct Collision2D
    {
        GameObjectHandle other;
        Component::BodyType2D bodyType = Component::BodyType2D::Dynamic;
        Vec2 point;
        Vec2 normal;
    };
}
