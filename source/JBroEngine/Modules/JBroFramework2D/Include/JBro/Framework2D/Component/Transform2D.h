#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    class Transform2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        Vec2  position;
        float rotation = 0.0f;
        Vec2  scale{ 1.0f, 1.0f };
    };

    class WorldTransform2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::WorldTransform2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        Matrix3x2 matrix;
        Vec2      position;
        float     rotation = 0.0f;
        Vec2      scale{ 1.0f, 1.0f };
        bool      dirty = true;
    };
}
