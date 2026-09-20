#include <JBro/Network/ServiceContext.h>

namespace JBro
{
    namespace
    {
        NetworkServiceContext g_serviceContext;
    }

    void BindNetworkServiceContext(const NetworkServiceContext& context)
    {
        g_serviceContext = context;
    }

    const NetworkServiceContext& GetNetworkServices()
    {
        return g_serviceContext;
    }
}
