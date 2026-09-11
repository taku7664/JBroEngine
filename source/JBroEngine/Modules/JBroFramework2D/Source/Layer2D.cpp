#include <JBro/Framework2D/Layer2D.h>

#include <algorithm>

namespace JBro
{
    Layer2D::BlendMode Layer2D::GetBlendMode() const
    {
        return m_blendMode;
    }

    void Layer2D::SetBlendMode(BlendMode mode)
    {
        m_blendMode = mode;
    }

    Layer2D::Space Layer2D::GetSpace() const
    {
        return m_space;
    }

    void Layer2D::SetSpace(Space space)
    {
        m_space = space;
    }

    float Layer2D::GetOpacity() const
    {
        return m_opacity;
    }

    void Layer2D::SetOpacity(float opacity)
    {
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float Layer2D::GetParallaxFactor() const
    {
        return m_parallaxFactor;
    }

    void Layer2D::SetParallaxFactor(float factor)
    {
        m_parallaxFactor = std::max(factor, 0.0f);
    }

    bool Layer2D::ForcesOwnTexture() const
    {
        return m_forceOwnTexture;
    }

    void Layer2D::SetForceOwnTexture(bool enabled)
    {
        m_forceOwnTexture = enabled;
    }
}
