#pragma once

#include <cstdint>

namespace JBro
{
    using LayerIndex = std::uint32_t;
    inline constexpr LayerIndex InvalidLayerIndex = static_cast<LayerIndex>(-1);

    enum class LayerBlendMode : std::uint8_t { Normal, Additive, Multiply, Screen };
    enum class LayerSpace     : std::uint8_t { World, Screen };

    class Layer final
    {
    public:
        Layer(LayerIndex index, const char* name);

        LayerIndex     GetIndex() const;
        const char*    GetName()  const;
        void           SetName(const char* name);

        LayerBlendMode GetBlendMode() const;
        void           SetBlendMode(LayerBlendMode mode);
        LayerSpace     GetSpace()     const;
        void           SetSpace(LayerSpace space);
        float          GetOpacity()   const;
        void           SetOpacity(float opacity);
        bool           IsVisible()    const;
        void           SetVisible(bool visible);
        float          GetParallaxFactor() const;
        void           SetParallaxFactor(float factor);
        bool           ForcesOwnTexture() const;
        void           SetForceOwnTexture(bool enabled);

    private:
        LayerIndex     m_index      = InvalidLayerIndex;
        char           m_name[64]{};
        LayerBlendMode m_blendMode  = LayerBlendMode::Normal;
        LayerSpace     m_space      = LayerSpace::World;
        float          m_opacity    = 1.0f;
        float          m_parallaxFactor = 1.0f;
        bool           m_visible    = true;
        bool           m_forceOwnTexture = false;
    };
}
