#include <JBro/Network/Transport.h>

#include <cstring>
#include <utility>

namespace JBro::Network
{
    namespace
    {
        // 1 단계 프레임: [uint32 LE 길이][uint16 LE 메시지 ID][uint8 채널][uint8 예약]. 2 단계가 WS 프레임으로 바꾼다.
        constexpr std::uint32_t FrameHeaderBytes = 8;

        void WriteHeader(std::uint8_t* out, std::uint32_t length, MessageId messageId, NetChannel channel)
        {
            out[0] = static_cast<std::uint8_t>(length & 0xFF);
            out[1] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
            out[2] = static_cast<std::uint8_t>((length >> 16) & 0xFF);
            out[3] = static_cast<std::uint8_t>((length >> 24) & 0xFF);
            out[4] = static_cast<std::uint8_t>(messageId & 0xFF);
            out[5] = static_cast<std::uint8_t>((messageId >> 8) & 0xFF);
            out[6] = static_cast<std::uint8_t>(channel);
            out[7] = 0;
        }

        void ReadHeader(const std::uint8_t* in, std::uint32_t& length, MessageId& messageId, NetChannel& channel)
        {
            length = static_cast<std::uint32_t>(in[0])
                | (static_cast<std::uint32_t>(in[1]) << 8)
                | (static_cast<std::uint32_t>(in[2]) << 16)
                | (static_cast<std::uint32_t>(in[3]) << 24);
            messageId = static_cast<MessageId>(in[4] | (in[5] << 8));
            channel = static_cast<NetChannel>(in[6]);
        }

        bool IsKnownChannel(NetChannel channel)
        {
            return static_cast<std::uint8_t>(channel) < NetChannelCount;
        }
    }

    Transport::Transport(ISocketProvider& provider, IClock& clock, const TransportConfig& config)
        : m_provider(provider)
        , m_clock(clock)
        , m_config(config)
    {
        m_connections.Reserve(m_config.maxConnections);
        m_inbound.Resize(m_config.inboundBytes);
        m_records.Resize(m_config.inboundMessages);
        m_events.Resize(m_config.eventCapacity);
        m_scratch.Resize(4096);
    }

    Transport::~Transport()
    {
        Close();
    }

    // ── 역할 ────────────────────────────────────────────────────────────────────────────────────

    bool Transport::Listen(std::uint16_t port)
    {
        if (m_role != NetworkRole::None)
        {
            return false;
        }
        OwnerPtr<IStreamSocket> listener = m_provider.CreateStreamSocket();
        if (nullptr == listener.Get())
        {
            return false;
        }
        if (false == listener->Listen(port))
        {
            return false;
        }
        m_listener = std::move(listener);
        m_role = NetworkRole::Server;
        return true;
    }

    bool Transport::Connect(const char* host, std::uint16_t port)
    {
        if (m_role != NetworkRole::None)
        {
            return false;
        }
        OwnerPtr<IStreamSocket> stream = m_provider.CreateStreamSocket();
        if (nullptr == stream.Get())
        {
            return false;
        }
        if (false == stream->Connect(host, port))
        {
            return false;
        }
        m_role = NetworkRole::Client;
        AddConnection(ServerConnectionId, std::move(stream), ConnectionState::Connecting);
        return true;
    }

    void Transport::Close()
    {
        for (Connection& connection : m_connections)
        {
            if (connection.state != ConnectionState::Disconnected)
            {
                PushEvent(NetworkEventKind::Disconnected, connection.id, DisconnectReason::Normal);
            }
            if (nullptr != connection.stream.Get())
            {
                connection.stream->Close();
            }
        }
        m_connections.Clear();
        if (nullptr != m_listener.Get())
        {
            m_listener->Close();
            m_listener = nullptr;
        }
        m_role = NetworkRole::None;
        m_nextClientId = ServerConnectionId + 1;
    }

    void Transport::CloseConnection(ConnectionId id)
    {
        Connection* connection = FindConnection(id);
        if (nullptr == connection)
        {
            return;
        }
        RequestClose(*connection, DisconnectReason::Normal);
    }

    NetworkRole Transport::GetRole() const
    {
        return m_role;
    }

    bool Transport::IsListening() const
    {
        return nullptr != m_listener.Get();
    }

    ConnectionState Transport::GetConnectionState(ConnectionId id) const
    {
        const Connection* connection = FindConnection(id);
        if (nullptr == connection)
        {
            return ConnectionState::Disconnected;
        }
        return connection->state;
    }

    std::uint32_t Transport::GetConnectionCount() const
    {
        return static_cast<std::uint32_t>(m_connections.Size());
    }

    ConnectionId Transport::GetConnectionAt(std::uint32_t index) const
    {
        if (index >= m_connections.Size())
        {
            return InvalidConnectionId;
        }
        return m_connections[index].id;
    }

    const TransportConfig& Transport::GetConfig() const
    {
        return m_config;
    }

    // ── 송신 ────────────────────────────────────────────────────────────────────────────────────

    bool Transport::Send(ConnectionId id, MessageId messageId, const void* data, std::uint32_t size, NetChannel channel)
    {
        if (size > m_config.maxMessageBytes || false == IsKnownChannel(channel))
        {
            return false;
        }
        if (size > 0 && nullptr == data)
        {
            return false;
        }
        Connection* connection = FindConnection(id);
        if (nullptr == connection || connection->state != ConnectionState::Connected || connection->wantsClose)
        {
            return false;
        }
        if (connection->send.Free() < FrameHeaderBytes + size)
        {
            return false;
        }
        std::uint8_t header[FrameHeaderBytes];
        WriteHeader(header, size, messageId, channel);
        connection->send.Write(header, FrameHeaderBytes);
        if (size > 0)
        {
            connection->send.Write(data, size);
        }
        FlushSend(*connection);
        return true;
    }

    bool Transport::Broadcast(MessageId messageId, const void* data, std::uint32_t size, NetChannel channel)
    {
        if (m_role != NetworkRole::Server)
        {
            return false;
        }
        bool any = false;
        for (Connection& connection : m_connections)
        {
            if (Send(connection.id, messageId, data, size, channel))
            {
                any = true;
            }
        }
        return any;
    }

    // ── 갱신 ────────────────────────────────────────────────────────────────────────────────────

    void Transport::Update()
    {
        CompactInbound();
        if (m_role == NetworkRole::Server)
        {
            AcceptPending();
        }
        for (Connection& connection : m_connections)
        {
            PollConnection(connection);
        }
        RemoveClosedConnections();
    }

    void Transport::CompactInbound()
    {
        if (0 == m_recordsTaken)
        {
            return;
        }
        if (m_recordsTaken >= m_recordCount)
        {
            m_recordCount = 0;
            m_recordsTaken = 0;
            m_inboundSize = 0;
            return;
        }
        // 꺼내 간 것은 앞쪽에 몰려 있다. 남은 바이트를 앞으로 당기고 오프셋을 그만큼 뺀다.
        const std::uint32_t firstKept = m_records[m_recordsTaken].offset;
        const std::uint32_t keptBytes = m_inboundSize - firstKept;
        std::memmove(m_inbound.Data(), m_inbound.Data() + firstKept, keptBytes);
        const std::uint32_t remaining = m_recordCount - m_recordsTaken;
        for (std::uint32_t index = 0; index < remaining; ++index)
        {
            InboundRecord& record = m_records[index];
            record = m_records[m_recordsTaken + index];
            record.offset -= firstKept;
        }
        m_recordCount = remaining;
        m_recordsTaken = 0;
        m_inboundSize = keptBytes;
    }

    void Transport::AcceptPending()
    {
        if (nullptr == m_listener.Get())
        {
            return;
        }
        while (m_connections.Size() < m_config.maxConnections)
        {
            OwnerPtr<IStreamSocket> accepted = m_listener->Accept();
            if (nullptr == accepted.Get())
            {
                break;
            }
            const ConnectionId id = m_nextClientId++;
            Connection& connection = AddConnection(id, std::move(accepted), ConnectionState::Connected);
            PushEvent(NetworkEventKind::Connected, connection.id, DisconnectReason::Normal);
        }
    }

    void Transport::PollConnection(Connection& connection)
    {
        if (connection.wantsClose || nullptr == connection.stream.Get())
        {
            return;
        }
        if (connection.state == ConnectionState::Connecting)
        {
            const ConnectionState socketState = connection.stream->GetState();
            if (socketState == ConnectionState::Connected)
            {
                connection.state = ConnectionState::Connected;
                PushEvent(NetworkEventKind::Connected, connection.id, DisconnectReason::Normal);
            }
            else if (socketState == ConnectionState::Disconnected)
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            else
            {
                return;
            }
        }
        FlushSend(connection);
        if (connection.wantsClose)
        {
            return;
        }
        ReadIntoRing(connection);
        ParseFrames(connection);
    }

    void Transport::FlushSend(Connection& connection)
    {
        while (false == connection.send.IsEmpty())
        {
            std::uint32_t runSize = 0;
            const std::uint8_t* run = connection.send.ContiguousData(runSize);
            std::size_t sent = 0;
            const SocketIo io = connection.stream->Send(run, runSize, sent);
            if (io == SocketIo::Ok)
            {
                if (0 == sent)
                {
                    break;
                }
                connection.send.Discard(static_cast<std::uint32_t>(sent));
                continue;
            }
            if (io == SocketIo::WouldBlock)
            {
                break;
            }
            RequestClose(connection, io == SocketIo::Closed ? DisconnectReason::Normal : DisconnectReason::Error);
            break;
        }
    }

    void Transport::ReadIntoRing(Connection& connection)
    {
        while (connection.receive.Free() > 0)
        {
            const std::uint32_t chunk = connection.receive.Free() < m_scratch.Size()
                ? connection.receive.Free()
                : static_cast<std::uint32_t>(m_scratch.Size());
            std::size_t received = 0;
            const SocketIo io = connection.stream->Receive(m_scratch.Data(), chunk, received);
            if (io == SocketIo::Ok)
            {
                if (0 == received)
                {
                    break;
                }
                connection.receive.Write(m_scratch.Data(), static_cast<std::uint32_t>(received));
                continue;
            }
            if (io == SocketIo::WouldBlock)
            {
                break;
            }
            RequestClose(connection, io == SocketIo::Closed ? DisconnectReason::Normal : DisconnectReason::Error);
            break;
        }
    }

    void Transport::ParseFrames(Connection& connection)
    {
        std::uint8_t header[FrameHeaderBytes];
        while (connection.receive.Size() >= FrameHeaderBytes)
        {
            connection.receive.Peek(header, FrameHeaderBytes);
            std::uint32_t length = 0;
            MessageId messageId = RawMessageId;
            NetChannel channel = NetChannel::ReliableOrdered;
            ReadHeader(header, length, messageId, channel);
            if (length > m_config.maxMessageBytes || false == IsKnownChannel(channel))
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            if (connection.receive.Size() < FrameHeaderBytes + length)
            {
                return;
            }
            // 저장소가 없으면 여기서 멈춘다. 프레임은 수신 고리에 남고, 고리가 차면 소켓 읽기가 멈춘다 - 그것이 역압이다.
            if (false == StoreInbound(connection, messageId, channel, length))
            {
                return;
            }
            connection.receive.Discard(FrameHeaderBytes + length);
        }
    }

    bool Transport::StoreInbound(const Connection& connection, MessageId messageId, NetChannel channel, std::uint32_t size)
    {
        if (m_recordCount >= m_records.Size())
        {
            return false;
        }
        if (m_inbound.Size() - m_inboundSize < size)
        {
            return false;
        }
        InboundRecord& record = m_records[m_recordCount];
        record.connection = connection.id;
        record.offset = m_inboundSize;
        record.size = size;
        record.messageId = messageId;
        record.channel = channel;
        if (size > 0)
        {
            connection.receive.PeekAt(FrameHeaderBytes, m_inbound.Data() + m_inboundSize, size);
        }
        m_inboundSize += size;
        ++m_recordCount;
        return true;
    }

    void Transport::RemoveClosedConnections()
    {
        std::size_t index = 0;
        while (index < m_connections.Size())
        {
            Connection& connection = m_connections[index];
            if (false == connection.wantsClose)
            {
                ++index;
                continue;
            }
            PushEvent(NetworkEventKind::Disconnected, connection.id, connection.closeReason);
            if (nullptr != connection.stream.Get())
            {
                connection.stream->Close();
            }
            m_connections.RemoveAt(index);
        }
        if (m_role == NetworkRole::Client && m_connections.IsEmpty())
        {
            m_role = NetworkRole::None;
        }
    }

    // ── 큐 ──────────────────────────────────────────────────────────────────────────────────────

    std::uint32_t Transport::TakeEvents(NetworkEvent* events, std::uint32_t capacity)
    {
        std::uint32_t taken = 0;
        while (taken < capacity && m_eventCount > 0)
        {
            events[taken] = m_events[m_eventHead];
            m_eventHead = (m_eventHead + 1) % static_cast<std::uint32_t>(m_events.Size());
            --m_eventCount;
            ++taken;
        }
        if (m_overflowPending && m_eventCount < m_events.Size())
        {
            m_overflowPending = false;
            PushEvent(NetworkEventKind::Overflow, InvalidConnectionId, DisconnectReason::Normal);
        }
        return taken;
    }

    std::uint32_t Transport::TakeMessages(MessageView* messages, std::uint32_t capacity)
    {
        std::uint32_t taken = 0;
        while (taken < capacity && m_recordsTaken < m_recordCount)
        {
            const InboundRecord& record = m_records[m_recordsTaken];
            MessageView& view = messages[taken];
            view.connection = record.connection;
            view.data = m_inbound.Data() + record.offset;
            view.size = record.size;
            view.messageId = record.messageId;
            view.channel = record.channel;
            ++m_recordsTaken;
            ++taken;
        }
        return taken;
    }

    // ── 내부 ────────────────────────────────────────────────────────────────────────────────────

    Transport::Connection* Transport::FindConnection(ConnectionId id)
    {
        for (Connection& connection : m_connections)
        {
            if (connection.id == id)
            {
                return &connection;
            }
        }
        return nullptr;
    }

    const Transport::Connection* Transport::FindConnection(ConnectionId id) const
    {
        for (const Connection& connection : m_connections)
        {
            if (connection.id == id)
            {
                return &connection;
            }
        }
        return nullptr;
    }

    Transport::Connection& Transport::AddConnection(ConnectionId id, OwnerPtr<IStreamSocket> stream, ConnectionState state)
    {
        Connection& connection = m_connections.Emplace();
        connection.id = id;
        connection.stream = std::move(stream);
        connection.state = state;
        connection.send.Reset(m_config.sendBufferBytes);
        connection.receive.Reset(m_config.receiveBufferBytes);
        return connection;
    }

    void Transport::RequestClose(Connection& connection, DisconnectReason reason)
    {
        if (connection.wantsClose)
        {
            return;
        }
        connection.wantsClose = true;
        connection.closeReason = reason;
    }

    void Transport::PushEvent(NetworkEventKind kind, ConnectionId connection, DisconnectReason reason)
    {
        if (m_eventCount >= m_events.Size())
        {
            m_overflowPending = true;
            return;
        }
        const std::uint32_t tail = (m_eventHead + m_eventCount) % static_cast<std::uint32_t>(m_events.Size());
        NetworkEvent& event = m_events[tail];
        event.kind = kind;
        event.connection = connection;
        event.reason = reason;
        ++m_eventCount;
    }
}
