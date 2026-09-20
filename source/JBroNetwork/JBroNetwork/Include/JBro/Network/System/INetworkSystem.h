#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Network/Replication/ReplicationTypes.h>
#include <JBro/Network/Types.h>

#include <cstdint>

namespace JBro::System
{
    // 값 서비스가 트랜스포트와 복제를 보는 창이다. 구현은 호스트의 `NetworkHost`(JBroNetworkSystem)이고, 스크립트 DLL 은
    // 컨텍스트 블록으로 받은 포인터를 통해서만 부른다. 메인 스레드 전용이다.
    class INetworkSystem
    {
    public:
        virtual ~INetworkSystem() = default;

        // ── 세션 ──
        virtual bool StartServer(std::uint16_t port) = 0;
        virtual bool Connect(const char* host, std::uint16_t port) = 0;
        virtual void Disconnect() = 0;
        virtual Network::NetworkRole GetRole() const = 0;
        // 서버는 듣고 있는가, 클라이언트는 hello 가 끝났는가.
        virtual bool IsConnected() const = 0;
        virtual std::uint32_t GetConnectionCount() const = 0;
        virtual Network::ConnectionId GetConnectionAt(std::uint32_t index) const = 0;
        virtual double GetRoundTripMilliseconds(Network::ConnectionId connection) const = 0;
        virtual double GetUdpLossRate(Network::ConnectionId connection) const = 0;

        // ── 메시지 ──
        virtual bool Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, std::uint32_t size,
            Network::NetChannel channel) = 0;
        virtual bool Broadcast(Network::MessageId messageId, const void* data, std::uint32_t size, Network::NetChannel channel) = 0;
        // 이번 프레임에 도착한 게임 메시지(복제 것은 뺀 것)를 꺼낸다. 뷰는 다음 프레임까지만 유효하다.
        virtual std::uint32_t TakeEvents(Network::NetworkEvent* events, std::uint32_t capacity) = 0;
        virtual std::uint32_t TakeMessages(Network::MessageView* messages, std::uint32_t capacity) = 0;

        // ── 복제 ──
        virtual Network::NetworkObjectId FindNetworkId(InstanceId object) const = 0;
        virtual InstanceId FindLocalObject(Network::NetworkObjectId id) const = 0;
        // 서버는 모든 오브젝트에 권한이 있고, 클라이언트는 아무것에도 없다(예측·소유 위임은 열어 둔 것).
        virtual bool HasAuthority(InstanceId object) const = 0;
    };
}
