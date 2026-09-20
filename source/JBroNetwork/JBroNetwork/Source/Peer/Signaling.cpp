#include <JBro/Network/Peer/Signaling.h>

#include <JBro/Network/Internal/UdpDatagram.h>

#include <cstring>

namespace JBro::Network
{
    // ── 서버 ────────────────────────────────────────────────────────────────────────────────────

    SignalingServer::SignalingServer(Transport& transport, const SignalingConfig& config)
        : m_transport(transport)
        , m_config(config)
    {
        m_rooms.Reserve(m_config.maxRooms);
        m_scratch.Resize(4 + m_config.maxSignalBytes);
    }

    void SignalingServer::Update()
    {
        NetworkEvent events[32];
        std::uint32_t count = 0;
        while ((count = m_transport.TakeEvents(events, 32)) > 0)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (events[index].kind == NetworkEventKind::Disconnected)
                {
                    HandleDisconnect(events[index].connection);
                }
            }
        }
        MessageView views[32];
        while ((count = m_transport.TakeMessages(views, 32)) > 0)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const MessageView& view = views[index];
                if (view.messageId == SignalingJoinMessage && view.size == 4)
                {
                    HandleJoin(view.connection, UdpProto::ReadU32(view.data));
                }
                else if (view.messageId == SignalingRelayMessage && view.size >= 4)
                {
                    HandleRelay(view.connection, view.data, view.size);
                }
            }
        }
    }

    std::uint32_t SignalingServer::GetRoomCount() const
    {
        return static_cast<std::uint32_t>(m_rooms.Size());
    }

    std::uint32_t SignalingServer::GetMemberCount(std::uint32_t room) const
    {
        for (const Room& candidate : m_rooms)
        {
            if (candidate.code == room)
            {
                return static_cast<std::uint32_t>(candidate.members.Size());
            }
        }
        return 0;
    }

    SignalingServer::Room* SignalingServer::FindRoom(std::uint32_t code)
    {
        for (Room& room : m_rooms)
        {
            if (room.code == code)
            {
                return &room;
            }
        }
        return nullptr;
    }

    SignalingServer::Room* SignalingServer::FindRoomOf(ConnectionId connection, bool& outIsHost, std::uint32_t& outPeerId)
    {
        for (Room& room : m_rooms)
        {
            if (room.host == connection)
            {
                outIsHost = true;
                outPeerId = 0;
                return &room;
            }
            for (const Member& member : room.members)
            {
                if (member.connection == connection)
                {
                    outIsHost = false;
                    outPeerId = member.peerId;
                    return &room;
                }
            }
        }
        return nullptr;
    }

    void SignalingServer::SendPeerId(ConnectionId to, MessageId messageId, std::uint32_t peerId)
    {
        std::uint8_t payload[4];
        UdpProto::WriteU32(payload, peerId);
        m_transport.Send(to, messageId, payload, 4, NetChannel::ReliableOrdered);
    }

    void SignalingServer::HandleJoin(ConnectionId connection, std::uint32_t code)
    {
        bool isHost = false;
        std::uint32_t peerId = 0;
        if (nullptr != FindRoomOf(connection, isHost, peerId))
        {
            // 이미 어느 방에 있다. 두 방에 있을 수는 없다.
            return;
        }
        Room* room = FindRoom(code);
        std::uint8_t role[5];
        if (nullptr == room)
        {
            if (m_rooms.Size() >= m_config.maxRooms)
            {
                return;
            }
            Room& created = m_rooms.Emplace();
            created.code = code;
            created.host = connection;
            created.members.Reserve(m_config.maxMembersPerRoom);
            role[0] = 1;
            UdpProto::WriteU32(role + 1, 0);
            m_transport.Send(connection, SignalingRoleMessage, role, 5, NetChannel::ReliableOrdered);
            return;
        }
        if (room->members.Size() >= m_config.maxMembersPerRoom)
        {
            return;
        }
        Member& member = room->members.Emplace();
        member.peerId = room->nextPeerId++;
        member.connection = connection;
        role[0] = 0;
        UdpProto::WriteU32(role + 1, member.peerId);
        m_transport.Send(connection, SignalingRoleMessage, role, 5, NetChannel::ReliableOrdered);
        SendPeerId(room->host, SignalingPeerJoinedMessage, member.peerId);
    }

    void SignalingServer::HandleRelay(ConnectionId connection, const std::uint8_t* payload, std::uint32_t size)
    {
        bool isHost = false;
        std::uint32_t peerId = 0;
        Room* room = FindRoomOf(connection, isHost, peerId);
        if (nullptr == room || size > m_scratch.Size())
        {
            return;
        }
        const std::uint32_t target = UdpProto::ReadU32(payload);
        std::memcpy(m_scratch.Data(), payload, size);
        if (isHost)
        {
            // 호스트 → 그 참가자. 참가자에게는 상대가 호스트(0)다.
            for (const Member& member : room->members)
            {
                if (member.peerId == target)
                {
                    UdpProto::WriteU32(m_scratch.Data(), 0);
                    m_transport.Send(member.connection, SignalingRelayMessage, m_scratch.Data(), size, NetChannel::ReliableOrdered);
                    return;
                }
            }
            return;
        }
        // 참가자 → 호스트. 호스트에게는 상대가 그 참가자다.
        UdpProto::WriteU32(m_scratch.Data(), peerId);
        m_transport.Send(room->host, SignalingRelayMessage, m_scratch.Data(), size, NetChannel::ReliableOrdered);
    }

    void SignalingServer::HandleDisconnect(ConnectionId connection)
    {
        for (std::size_t index = 0; index < m_rooms.Size(); ++index)
        {
            Room& room = m_rooms[index];
            if (room.host == connection)
            {
                // 호스트가 떠났다. 참가자들에게 알리고 방을 닫는다.
                for (const Member& member : room.members)
                {
                    SendPeerId(member.connection, SignalingPeerLeftMessage, 0);
                }
                m_rooms.RemoveAt(index);
                return;
            }
            for (std::size_t at = 0; at < room.members.Size(); ++at)
            {
                if (room.members[at].connection == connection)
                {
                    SendPeerId(room.host, SignalingPeerLeftMessage, room.members[at].peerId);
                    room.members.RemoveAt(at);
                    return;
                }
            }
        }
    }

    // ── 클라이언트 ──────────────────────────────────────────────────────────────────────────────

    SignalingClient::SignalingClient(Transport& signaling, Transport& peers, const SignalingConfig& config)
        : m_signaling(signaling)
        , m_peers(peers)
        , m_config(config)
    {
        m_mappings.Reserve(m_config.maxMembersPerRoom);
        m_scratch.Resize(4 + m_config.maxSignalBytes);
    }

    void SignalingClient::Join(std::uint32_t room)
    {
        m_room = room;
        m_joinPending = true;
    }

    void SignalingClient::Update()
    {
        if (m_joinPending && m_signaling.GetConnectionState(ServerConnectionId) == ConnectionState::Connected)
        {
            std::uint8_t payload[4];
            UdpProto::WriteU32(payload, m_room);
            if (m_signaling.Send(ServerConnectionId, SignalingJoinMessage, payload, 4, NetChannel::ReliableOrdered))
            {
                m_joinPending = false;
            }
        }
        NetworkEvent events[16];
        while (m_signaling.TakeEvents(events, 16) > 0)
        {
            // 시그널링 연결의 사건은 여기서 삼킨다. 끊기면 새 시그널은 오지 않지만 이미 이어진 피어는 그대로 산다.
        }
        MessageView views[16];
        std::uint32_t count = 0;
        while ((count = m_signaling.TakeMessages(views, 16)) > 0)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const MessageView& view = views[index];
                if (view.messageId == SignalingRoleMessage && view.size == 5)
                {
                    HandleRole(0 != view.data[0], UdpProto::ReadU32(view.data + 1));
                }
                else if (view.messageId == SignalingPeerJoinedMessage && view.size == 4)
                {
                    HandlePeerJoined(UdpProto::ReadU32(view.data));
                }
                else if (view.messageId == SignalingPeerLeftMessage && view.size == 4)
                {
                    HandlePeerLeft(UdpProto::ReadU32(view.data));
                }
                else if (view.messageId == SignalingRelayMessage && view.size > 4)
                {
                    HandleRelay(UdpProto::ReadU32(view.data), view.data + 4, view.size - 4);
                }
            }
        }
        ForwardSignals();
        // 끊어진 피어의 대응을 잊는다.
        std::size_t index = 0;
        while (index < m_mappings.Size())
        {
            if (m_peers.GetConnectionState(m_mappings[index].connection) == ConnectionState::Disconnected
                && m_peers.GetConnectionKind(m_mappings[index].connection) == ConnectionKind::Socket)
            {
                // 종류가 Socket 으로 보이는 것은 연결이 이미 표에서 사라진 것이다.
                m_mappings.RemoveAt(index);
                continue;
            }
            ++index;
        }
    }

    void SignalingClient::HandleRole(bool isHost, std::uint32_t peerId)
    {
        if (m_hasRole)
        {
            return;
        }
        m_hasRole = true;
        m_isHost = isHost;
        m_peerId = peerId;
        if (isHost)
        {
            m_peers.HostPeers();
            return;
        }
        if (m_peers.ConnectPeer())
        {
            Mapping& mapping = m_mappings.Emplace();
            mapping.peerId = 0;
            mapping.connection = ServerConnectionId;
        }
    }

    void SignalingClient::HandlePeerJoined(std::uint32_t peerId)
    {
        if (false == m_isHost || m_mappings.Size() >= m_config.maxMembersPerRoom)
        {
            return;
        }
        const ConnectionId connection = m_peers.AcceptPeer();
        if (InvalidConnectionId == connection)
        {
            return;
        }
        Mapping& mapping = m_mappings.Emplace();
        mapping.peerId = peerId;
        mapping.connection = connection;
    }

    void SignalingClient::HandlePeerLeft(std::uint32_t peerId)
    {
        for (std::size_t index = 0; index < m_mappings.Size(); ++index)
        {
            if (m_mappings[index].peerId == peerId)
            {
                m_peers.CloseConnection(m_mappings[index].connection);
                m_mappings.RemoveAt(index);
                return;
            }
        }
    }

    void SignalingClient::HandleRelay(std::uint32_t peerId, const std::uint8_t* signal, std::uint32_t size)
    {
        const ConnectionId connection = FindPeerConnection(peerId);
        if (InvalidConnectionId == connection)
        {
            return;
        }
        m_peers.PushPeerSignal(connection, signal, size);
    }

    void SignalingClient::ForwardSignals()
    {
        for (const Mapping& mapping : m_mappings)
        {
            while (true)
            {
                const std::uint32_t size = m_peers.TakePeerSignal(mapping.connection, m_scratch.Data() + 4, m_config.maxSignalBytes);
                if (0 == size)
                {
                    break;
                }
                UdpProto::WriteU32(m_scratch.Data(), mapping.peerId);
                m_signaling.Send(ServerConnectionId, SignalingRelayMessage, m_scratch.Data(), 4 + size, NetChannel::ReliableOrdered);
            }
        }
    }

    bool SignalingClient::HasRole() const
    {
        return m_hasRole;
    }

    bool SignalingClient::IsHost() const
    {
        return m_isHost;
    }

    std::uint32_t SignalingClient::GetPeerId() const
    {
        return m_peerId;
    }

    ConnectionId SignalingClient::FindPeerConnection(std::uint32_t peerId) const
    {
        for (const Mapping& mapping : m_mappings)
        {
            if (mapping.peerId == peerId)
            {
                return mapping.connection;
            }
        }
        return InvalidConnectionId;
    }

    std::uint32_t SignalingClient::GetPeerCount() const
    {
        return static_cast<std::uint32_t>(m_mappings.Size());
    }
}
