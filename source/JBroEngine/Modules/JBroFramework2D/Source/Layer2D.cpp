#include <JBro/Framework2D/Layer2D.h>

#include <algorithm>

namespace JBro
{
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
