#include <JBro/Framework3D/ServiceContext.h>

namespace JBro
{
    namespace
    {
        Framework3DServiceContext g_serviceContext;
    }

    void BindFramework3DServiceContext(const Framework3DServiceContext& context)
    {
        g_serviceContext = context;
    }

    const Framework3DServiceContext& GetFramework3DServices()
    {
        return g_serviceContext;
    }
}
