#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro
{
    class NetworkHost;
}

namespace JBro::System
{
    // 고정 스텝의 **가장 앞**. 클라이언트가 최신 스냅숏을 풀에 입힌다. 시뮬레이션이 그 위에서 돈다.
    class NetworkReceiveSystem final : public GameSystem
    {
    public:
        explicit NetworkReceiveSystem(NetworkHost& host);
        int GetExecutionOrder() const override;

    protected:
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;

    private:
        NetworkHost& m_host;
    };

    // 고정 스텝의 **가장 뒤**. 서버가 스냅숏을 찍어 보낸다. 물리가 끝난 뒤의 상태다.
    class NetworkSendSystem final : public GameSystem
    {
    public:
        explicit NetworkSendSystem(NetworkHost& host);
        int GetExecutionOrder() const override;

    protected:
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;

    private:
        NetworkHost& m_host;
    };
}
