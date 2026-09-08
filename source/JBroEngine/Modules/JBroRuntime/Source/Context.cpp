#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

namespace JBro
{
    namespace
    {
        SystemContext g_systemContext;
        ServiceContext g_serviceContext;
    }

    void BindSystemContext(const SystemContext& context)
    {
        g_systemContext = context;
    }

    const SystemContext& GetSystemContext()
    {
        return g_systemContext;
    }

    void BindServiceContext(const ServiceContext& context)
    {
        g_serviceContext = context;
    }

    const ServiceContext& GetServiceContext()
    {
        return g_serviceContext;
    }
}
