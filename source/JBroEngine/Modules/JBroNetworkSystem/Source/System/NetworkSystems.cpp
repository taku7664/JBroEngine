#include <JBro/NetworkSystem/System/NetworkSystems.h>

#include <JBro/NetworkSystem/NetworkHost.h>

namespace JBro::System
{
    NetworkReceiveSystem::NetworkReceiveSystem(NetworkHost& host)
        : m_host(host)
    {
    }

    int NetworkReceiveSystem::GetExecutionOrder() const
    {
        // Transform2DSystem(100) 앞이다.
        return 50;
    }

    void NetworkReceiveSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        (void)canvas;
        (void)fixedDeltaTime;
        m_host.ApplyClient(1.0f);
    }

    NetworkSendSystem::NetworkSendSystem(NetworkHost& host)
        : m_host(host)
    {
    }

    int NetworkSendSystem::GetExecutionOrder() const
    {
        // SpriteRender2DSystem(400) 뒤다.
        return 500;
    }

    void NetworkSendSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        (void)canvas;
        (void)fixedDeltaTime;
        m_host.StepServer();
    }
}
