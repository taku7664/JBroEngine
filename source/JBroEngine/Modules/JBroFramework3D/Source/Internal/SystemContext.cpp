#include <JBro/Framework3D/Internal/SystemContext.h>

namespace JBro
{
    namespace
    {
        Framework3DSystemContext g_systemContext;
    }

    void BindFramework3DSystemContext(const Framework3DSystemContext& context)
    {
        g_systemContext = context;
    }

    const Framework3DSystemContext& GetFramework3DSystems()
    {
        return g_systemContext;
    }
}
