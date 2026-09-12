#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>

namespace JBro::Component
{
    enum class SpriteFlip : std::uint8_t { None, Horizontal, Vertical, Both };

    class SpriteRenderer2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::SpriteRenderer2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        AssetHandle  sprite;
        AssetHandle  material;
        Color        tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vec2         pivot{ 0.5f, 0.5f };
        Vec2         size { 1.0f, 1.0f };
        SpriteFlip   flip        = SpriteFlip::None;
        std::int32_t renderOrder = 0;
        bool         visible     = true;
    };
}
