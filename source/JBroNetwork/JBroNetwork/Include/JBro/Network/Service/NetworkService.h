#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Network/Replication/ReplicationTypes.h>
#include <JBro/Network/Types.h>

#include <cstdint>
#include <type_traits>

namespace JBro::Service
{
    // 메시지와 복제. 메인 스레드 전용이고 상태가 없다. 시스템이 없으면 거짓·0·`Invalid` 다.
    class NetworkService
    {
    public:
        // 메시지 ID 는 1..0xFDFF 다. 0xFE00~ 은 복제, 0xFF00~ 은 세션이 쓴다. 채널은 의도만 말한다 - UDP 가 없으면 WS 로 간다.
        bool Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, std::uint32_t size,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const;
        // 서버 전용.
        bool Broadcast(Network::MessageId messageId, const void* data, std::uint32_t size,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const;
        // 이번 프레임의 게임 메시지. 뷰는 다음 프레임까지만 유효하다.
        std::uint32_t TakeMessages(Network::MessageView* messages, std::uint32_t capacity) const;

        // POD 메시지 편의. 호스트와 DLL 이 다른 컴파일러일 수 있어 직렬화는 memcpy(LE 고정)이고 타입은 POD 여야 한다.
        template <typename T>
        bool Send(Network::ConnectionId connection, Network::MessageId messageId, const T& message,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "network messages must be POD - the DLL boundary rule");
            return Send(connection, messageId, &message, static_cast<std::uint32_t>(sizeof(T)), channel);
        }

        template <typename T>
        bool Broadcast(Network::MessageId messageId, const T& message,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "network messages must be POD - the DLL boundary rule");
            return Broadcast(messageId, &message, static_cast<std::uint32_t>(sizeof(T)), channel);
        }

        // 복제 표. 없으면 `InvalidNetworkObjectId` / `InvalidInstanceId`.
        Network::NetworkObjectId FindNetworkId(InstanceId object) const;
        InstanceId FindLocalObject(Network::NetworkObjectId id) const;
        bool HasAuthority(InstanceId object) const;
    };
}
