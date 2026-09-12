#include <JBro/Framework2D/Internal/SystemContext.h>

namespace JBro
{
    namespace
    {
        Framework2DSystemContext g_systemContext;
    }

    void BindFramework2DSystemContext(const Framework2DSystemContext& context)
    {
        g_systemContext = context;
    }

    const Framework2DSystemContext& GetFramework2DSystems()
    {
        return g_systemContext;
    }
}
