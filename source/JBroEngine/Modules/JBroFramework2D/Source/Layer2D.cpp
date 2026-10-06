#include <JBro/Framework2D/Layer2D.h>

#include <algorithm>

namespace JBro
{
    bool Layer2D::ForcesOwnTexture() const
    {
        return m_forceOwnTexture;
    }

    void Layer2D::SetForceOwnTexture(bool enabled)
    {
        m_forceOwnTexture = enabled;
    }
}
