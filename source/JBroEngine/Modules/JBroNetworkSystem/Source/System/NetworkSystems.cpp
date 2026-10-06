#include <JBro/NetworkSystem/System/NetworkSystems.h>

#include <JBro/NetworkSystem/NetworkHost.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    NetworkReceiveSystem::NetworkReceiveSystem(NetworkHost& host)
        : m_host(host)
    {
    }

    Int32 NetworkReceiveSystem::GetExecutionOrder() const
    {
        // Transform2DSystem(100) 앞이다.
        return 50;
    }

    void NetworkReceiveSystem::OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime)
    {
        (void)canvas;
        (void)fixedDeltaTime;
        m_host.ApplyClient();
    }

    NetworkSendSystem::NetworkSendSystem(NetworkHost& host)
        : m_host(host)
    {
    }

    Int32 NetworkSendSystem::GetExecutionOrder() const
    {
        // SpriteRender2DSystem(400) 뒤다.
        return 500;
    }

    void NetworkSendSystem::OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime)
    {
        (void)canvas;
        (void)fixedDeltaTime;
        m_host.StepServer();
    }
}
