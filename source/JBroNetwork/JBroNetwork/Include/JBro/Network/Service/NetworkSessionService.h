#pragma once

#include <JBro/Network/Types.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 연결·역할·품질. 메인 스레드 전용이고 상태가 없다 - 컨텍스트에 바인딩된 시스템에 위임하며, 시스템이 없으면
    // 거짓·`None`·-1 을 돌려준다(`Physics2DService` 규약).
    class NetworkSessionService
    {
    public:
        Bool StartServer(std::uint16_t port) const;
        // `host` 는 `ws://` 접두를 받는다. 결과는 `TakeEvents` 의 `Connected` / `Disconnected` 로 온다.
        Bool Connect(const char* host, std::uint16_t port) const;
        void Disconnect() const;

        Network::NetworkRole GetRole() const;
        Bool IsConnected() const;
        UInt32 GetConnectionCount() const;
        Network::ConnectionId GetConnectionAt(UInt32 index) const;
        // 아직 재지 않았거나 모르면 -1.
        double GetRoundTripMilliseconds(Network::ConnectionId connection) const;
        // 표본이 없거나 UDP 가 없으면 -1.
        double GetUdpLossRate(Network::ConnectionId connection) const;
        // 채운 개수. 남은 것은 다음에 이어진다.
        UInt32 TakeEvents(Network::NetworkEvent* events, UInt32 capacity) const;
    };
}
