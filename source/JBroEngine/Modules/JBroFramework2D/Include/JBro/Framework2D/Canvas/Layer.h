#pragma once

#include <cstdint>

namespace JBro::Engine
{
    using LayerId = std::uint32_t;
    inline constexpr LayerId InvalidLayerId = static_cast<LayerId>(-1);

    enum class ELayerBlendMode : std::uint8_t
    {
        Normal,
        Additive,
        Multiply,
        Screen
    };

    enum class ELayerSpace : std::uint8_t
    {
        World,
        Screen
    };

    class CLayer final
    {
    public:
        CLayer(LayerId id, const char* name);

        LayerId GetId() const;
        const char* GetName() const;
        void SetName(const char* name);

        ELayerBlendMode GetBlendMode() const;
        void SetBlendMode(ELayerBlendMode mode);
        ELayerSpace GetSpace() const;
        void SetSpace(ELayerSpace space);
        float GetOpacity() const;
        void SetOpacity(float opacity);
        bool IsVisible() const;
        void SetVisible(bool visible);
        float GetParallaxFactor() const;
        void SetParallaxFactor(float factor);
        bool ForcesOwnTexture() const;
        void SetForceOwnTexture(bool enabled);

    private:
        LayerId m_id = InvalidLayerId;
        char m_name[64]{};
        ELayerBlendMode m_blendMode = ELayerBlendMode::Normal;
        ELayerSpace m_space = ELayerSpace::World;
        float m_opacity = 1.0f;
        float m_parallaxFactor = 1.0f;
        bool m_visible = true;
        bool m_forceOwnTexture = false;
    };
}
