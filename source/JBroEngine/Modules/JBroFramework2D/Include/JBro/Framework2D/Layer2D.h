#pragma once

#include <cstdint>

namespace JBro
{
    class Layer2D final
    {
    public:
        enum class BlendMode : std::uint8_t
        {
            Normal,
            Additive,
            Multiply,
            Screen
        };

        BlendMode GetBlendMode() const;
        void SetBlendMode(BlendMode mode);

        float GetOpacity() const;
        void SetOpacity(float opacity);

        float GetParallaxFactor() const;
        void SetParallaxFactor(float factor);

        bool ForcesOwnTexture() const;
        void SetForceOwnTexture(bool enabled);

    private:
        BlendMode m_blendMode      = BlendMode::Normal;
        float     m_opacity        = 1.0f;
        float     m_parallaxFactor = 1.0f;
        bool      m_forceOwnTexture = false;
    };
}
