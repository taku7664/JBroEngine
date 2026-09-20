#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Uuid.h>

#include <cstdint>
#include <type_traits>

// 복제의 공개 값 타입이다(network-plan §2.6). 컴포넌트 풀을 타입 단위로 등록하고, 서버가 고정 스텝마다 스냅숏을 찍어
// 클라이언트가 마지막으로 ACK 한 기준과의 차이만 보낸다. 오브젝트는 컴포넌트가 아니라 표로 잇는다 - `GameObjectHandle` 은
// 프로세스 지역 값이라 와이어에 못 쓴다.
namespace JBro::Network
{
    // 네트워크상 오브젝트 식별자. 서버가 붙이고 양쪽 표가 지역 `InstanceId` 로 되푼다.
    using NetworkObjectId = std::uint32_t;
    inline constexpr NetworkObjectId InvalidNetworkObjectId = 0;

    // 서버의 고정 스텝 번호. 스냅숏은 이것으로 불린다.
    using ReplicationTick = std::uint32_t;
    // "기준이 없다" - 전체 스냅숏이다.
    inline constexpr ReplicationTick NoBaselineTick = 0xFFFFFFFFu;

    // 복제가 쓰는 메시지 ID 대역. 게임은 이 대역을 쓰지 않는다. 세션 대역(0xFF00~) 바로 아래다.
    inline constexpr MessageId FirstReplicationMessageId = 0xFE00;
    inline constexpr MessageId ReplicationSpawnMessage = 0xFE01;
    inline constexpr MessageId ReplicationDespawnMessage = 0xFE02;
    inline constexpr MessageId ReplicationDeltaMessage = 0xFE03;
    inline constexpr MessageId ReplicationAckMessage = 0xFE04;

    inline bool IsReplicationMessage(MessageId id)
    {
        return id >= FirstReplicationMessageId && id < FirstSystemMessageId;
    }

    // 클라이언트가 오브젝트를 만들 때 필요한 것. 서버의 `IReplicationHost::DescribeObject` 가 채운다.
    struct SpawnDesc
    {
        // 어떤 프리팹인가. 에셋 `Uuid` 다.
        Uuid prefab;
        // 소유 클라이언트. 서버 소유면 `InvalidConnectionId`.
        ConnectionId owner = InvalidConnectionId;
        std::uint32_t flags = 0;
        std::uint32_t reserved = 0;
    };

    struct ReplicationConfig
    {
        // 동시에 살아 있는 네트워크 오브젝트 상한.
        std::uint32_t maxObjects = 4096;
        // 등록할 수 있는 풀 타입 수.
        std::uint32_t maxTypes = 16;
        // 기준으로 되돌아볼 수 있는 스냅숏 수. 클라이언트의 ACK 가 이보다 뒤처지면 전체 스냅숏을 다시 보낸다.
        std::uint32_t historyTicks = 32;
        // 스냅숏 하나의 항목(오브젝트 × 타입)과 바이트 상한.
        std::uint32_t maxEntriesPerSnapshot = 8192;
        std::uint32_t snapshotBytes = 512 * 1024;
        // 델타 메시지 하나의 상한. 트랜스포트의 최대 메시지 크기 이하여야 한다. 넘치면 그 틱은 보내지 않고 센다.
        std::uint32_t maxDeltaBytes = 60 * 1024;
    };

    static_assert(std::is_standard_layout_v<SpawnDesc>);
    static_assert(std::is_trivially_copyable_v<SpawnDesc>);
    static_assert(sizeof(SpawnDesc) == 32);
}
