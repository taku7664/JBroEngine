#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Framework2D/Math2D.h>

#include <cstdint>

namespace JBro::Component
{
    enum class SpriteFlip : std::uint8_t { None, Horizontal, Vertical, Both };

    struct SpriteRenderer2D
    {
        AssetHandle  sprite;
        AssetHandle  material;
        Color        tint;
        Vec2         pivot{ 0.5f, 0.5f };
        Vec2         size { 1.0f, 1.0f };
        SpriteFlip   flip        = SpriteFlip::None;
        std::int32_t renderOrder = 0;
        bool         visible     = true;
    };
}
