#include <JBro/Framework2D/Layer2D.h>

#include <algorithm>
#include <JBro/Types/Bool.h>

namespace JBro
{
    Bool Layer2D::ForcesOwnTexture() const
    {
        return m_forceOwnTexture;
    }

    void Layer2D::SetForceOwnTexture(Bool enabled)
    {
        m_forceOwnTexture = enabled;
    }
}
