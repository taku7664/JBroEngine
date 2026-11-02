#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Network/Replication/ReplicationTypes.h>
#include <JBro/Network/Types.h>

#include <cstdint>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 메시지와 복제. 메인 스레드 전용이고 상태가 없다. 시스템이 없으면 거짓·0·`Invalid` 다.
    class NetworkService
    {
    public:
        // 메시지 ID 는 1..0xFDFF 다. 0xFE00~ 은 복제, 0xFF00~ 은 세션이 쓴다. 채널은 의도만 말한다 - UDP 가 없으면 WS 로 간다.
        Bool Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, UInt32 size,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const;
        // 서버 전용.
        Bool Broadcast(Network::MessageId messageId, const void* data, UInt32 size,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const;
        // 이번 프레임의 게임 메시지. 뷰는 다음 프레임까지만 유효하다.
        UInt32 TakeMessages(Network::MessageView* messages, UInt32 capacity) const;

        // POD 메시지 편의. 호스트와 DLL 이 다른 컴파일러일 수 있어 직렬화는 memcpy(LE 고정)이고 타입은 POD 여야 한다.
        template <typename T>
        Bool Send(Network::ConnectionId connection, Network::MessageId messageId, const T& message,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "network messages must be POD - the DLL boundary rule");
            return Send(connection, messageId, &message, static_cast<JBro::UInt32>(sizeof(T)), channel);
        }

        template <typename T>
        Bool Broadcast(Network::MessageId messageId, const T& message,
            Network::NetChannel channel = Network::NetChannel::ReliableOrdered) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "network messages must be POD - the DLL boundary rule");
            return Broadcast(messageId, &message, static_cast<JBro::UInt32>(sizeof(T)), channel);
        }

        // 복제 표. 없으면 `InvalidNetworkObjectId` / `InvalidInstanceId`.
        Network::NetworkObjectId FindNetworkId(InstanceId object) const;
        InstanceId FindLocalObject(Network::NetworkObjectId id) const;
        // **이 오브젝트를 내가 쓸 수 있는가.** 서버는 자기가 가진 것에, 클라이언트는 서버가 자기에게 넘긴 것에 참이다.
        // 거짓인 쪽에서 상태를 바꾸면 다음 스냅숏이 덮어쓴다.
        Bool HasAuthority(InstanceId object) const;
        // 서버 전용. 오브젝트를 어느 연결에 넘긴다. `InvalidConnectionId` 면 서버가 되찾는다.
        Bool SetObjectOwner(InstanceId object, Network::ConnectionId owner) const;
        Network::ConnectionId GetObjectOwner(InstanceId object) const;
    };
}
