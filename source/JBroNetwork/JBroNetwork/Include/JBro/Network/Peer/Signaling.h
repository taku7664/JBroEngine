#pragma once

#include <JBro/Network/Transport.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

// WebRTC 시그널링(network-plan §2.7). 두 브라우저는 서로를 직접 찾지 못하므로, 작은 WS 서버가 방 코드로 둘을 만나게 하고
// SDP 제안·응답과 ICE 후보를 중계한다. 게임 데이터는 여기를 지나지 않는다. 서버는 네이티브 게임 호스트의 기능이다 -
// 같은 `Transport` 로 WS 를 받으니 새 소켓 코드가 없다.
//
// 메시지 대역 0xFD00~ 은 시그널링이 쓴다. 게임은 이 대역을 쓰지 않는다.
namespace JBro::Network
{
    inline constexpr MessageId FirstSignalingMessageId = 0xFD00;
    // 클라이언트 → 서버. [uint32 room]
    inline constexpr MessageId SignalingJoinMessage = 0xFD01;
    // 서버 → 클라이언트. [uint8 isHost][uint32 peerId]. 방의 첫 사람이 호스트다.
    inline constexpr MessageId SignalingRoleMessage = 0xFD02;
    // 서버 → 호스트. [uint32 peerId]
    inline constexpr MessageId SignalingPeerJoinedMessage = 0xFD03;
    // 서버 → 호스트 [uint32 peerId], 또는 서버 → 참가자 [0](호스트가 떠났다).
    inline constexpr MessageId SignalingPeerLeftMessage = 0xFD04;
    // [uint32 peerId][bytes]. 참가자 → 서버는 peerId 를 무시하고 호스트로, 호스트 → 서버는 그 참가자로.
    inline constexpr MessageId SignalingRelayMessage = 0xFD05;

    inline Bool IsSignalingMessage(MessageId id)
    {
        return id >= FirstSignalingMessageId && id < 0xFE00;
    }

    struct SignalingConfig
    {
        UInt32 maxRooms = 64;
        UInt32 maxMembersPerRoom = 32;
        // 시그널 하나(SDP 는 몇 KB 다)의 상한.
        UInt32 maxSignalBytes = 16 * 1024;
    };

    // 방과 중계. 넘겨받은 트랜스포트(서버 역할)의 이벤트와 메시지를 **혼자** 소비한다.
    class SignalingServer final
    {
    public:
        SignalingServer(Transport& transport, const SignalingConfig& config = {});

        // 트랜스포트 `Update` 뒤에 부른다.
        void Update();
        UInt32 GetRoomCount() const;
        UInt32 GetMemberCount(UInt32 room) const;

    private:
        struct Member
        {
            UInt32 peerId = 0;
            ConnectionId connection = InvalidConnectionId;
        };

        struct Room
        {
            UInt32 code = 0;
            ConnectionId host = InvalidConnectionId;
            Array<Member> members;
            UInt32 nextPeerId = 1;
        };

        Room* FindRoom(UInt32 code);
        Room* FindRoomOf(ConnectionId connection, Bool& outIsHost, UInt32& outPeerId);
        void HandleJoin(ConnectionId connection, UInt32 code);
        void HandleRelay(ConnectionId connection, const std::uint8_t* payload, UInt32 size);
        void HandleDisconnect(ConnectionId connection);
        void SendPeerId(ConnectionId to, MessageId messageId, UInt32 peerId);

        Transport& m_transport;
        SignalingConfig m_config;
        Array<Room> m_rooms;
        Array<std::uint8_t> m_scratch;
    };

    // 시그널링 클라이언트. 시그널링 트랜스포트(WS 클라이언트)와 피어 트랜스포트를 잇는다: 역할을 받으면 피어 트랜스포트를
    // 호스트 또는 클라이언트로 세우고, 시그널을 양쪽으로 중계한다. 시그널링 트랜스포트의 이벤트와 메시지를 **혼자** 소비한다.
    class SignalingClient final
    {
    public:
        SignalingClient(Transport& signaling, Transport& peers, const SignalingConfig& config = {});

        // 시그널링 연결이 준비되면 방에 들어간다.
        void Join(UInt32 room);
        // 두 트랜스포트의 `Update` 뒤에 부른다.
        void Update();

        Bool HasRole() const;
        Bool IsHost() const;
        UInt32 GetPeerId() const;
        // 이 참가자와 이어진 피어 연결. 참가자 쪽은 peerId 0 이 호스트다.
        ConnectionId FindPeerConnection(UInt32 peerId) const;
        UInt32 GetPeerCount() const;

    private:
        struct Mapping
        {
            UInt32 peerId = 0;
            ConnectionId connection = InvalidConnectionId;
        };

        void HandleRole(Bool isHost, UInt32 peerId);
        void HandlePeerJoined(UInt32 peerId);
        void HandlePeerLeft(UInt32 peerId);
        void HandleRelay(UInt32 peerId, const std::uint8_t* signal, UInt32 size);
        void ForwardSignals();

        Transport& m_signaling;
        Transport& m_peers;
        SignalingConfig m_config;
        Array<Mapping> m_mappings;
        Array<std::uint8_t> m_scratch;
        UInt32 m_room = 0;
        Bool m_joinPending = false;
        Bool m_hasRole = false;
        Bool m_isHost = false;
        UInt32 m_peerId = 0;
    };
}
