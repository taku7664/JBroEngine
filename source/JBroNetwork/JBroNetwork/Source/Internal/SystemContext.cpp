#include <JBro/Network/Internal/SystemContext.h>

namespace JBro
{
    namespace
    {
        NetworkSystemContext g_systemContext;
    }

    void BindNetworkSystemContext(const NetworkSystemContext& context)
    {
        g_systemContext = context;
    }

    const NetworkSystemContext& GetNetworkSystems()
    {
        return g_systemContext;
    }
}
