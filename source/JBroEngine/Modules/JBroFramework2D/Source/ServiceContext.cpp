#include <JBro/Framework2D/ServiceContext.h>

namespace JBro
{
    namespace
    {
        Framework2DServiceContext g_serviceContext;
    }

    void BindFramework2DServiceContext(const Framework2DServiceContext& context)
    {
        g_serviceContext = context;
    }

    const Framework2DServiceContext& GetFramework2DServices()
    {
        return g_serviceContext;
    }
}
