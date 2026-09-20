#include <JBro/Network/Transport.h>

#include <cstring>
#include <utility>

namespace JBro::Network
{
    namespace
    {
        // WS 메시지 안의 헤더: [uint16 LE 메시지 ID]. 기존 엔진과 같다.
        constexpr std::uint32_t MessageHeaderBytes = 2;
        // 핸드셰이크 HTTP 블록의 상한. 이보다 길면 위반으로 본다.
        constexpr std::uint32_t MaxHandshakeBytes = 4096;

        // 세션 시스템 메시지(0xFF00~). 유저는 등록할 수 없다.
        constexpr MessageId SystemHello = 0xFF01;
        constexpr MessageId SystemHelloAck = 0xFF02;
        constexpr MessageId SystemBye = 0xFF03;
        constexpr MessageId SystemPing = 0xFF04;
        constexpr MessageId SystemPong = 0xFF05;

        struct HelloPayload
        {
            std::uint32_t protocolVersion = 0;
        };

        struct ByePayload
        {
            std::uint8_t reason = 0;
        };

        struct PingPayload
        {
            double sendMilliseconds = 0.0;
        };

        MessageId ReadMessageId(const std::uint8_t* bytes)
        {
            return static_cast<MessageId>(bytes[0] | (bytes[1] << 8));
        }

        bool IsKnownChannel(NetChannel channel)
        {
            return static_cast<std::uint8_t>(channel) < NetChannelCount;
        }

        // `ws://` 접두를 벗긴다. `wss://` 는 아직 받지 않는다(TLS 는 §3-7).
        const char* StripScheme(const char* host)
        {
            if (0 == std::strncmp(host, "ws://", 5))
            {
                return host + 5;
            }
            return host;
        }

        std::uint32_t Max(std::uint32_t a, std::uint32_t b)
        {
            return a > b ? a : b;
        }
    }

    Transport::Transport(ISocketProvider& provider, IClock& clock, const TransportConfig& config)
        : m_provider(provider)
        , m_clock(clock)
        , m_config(config)
    {
        // 한 프레임이 통째로 들어갈 자리가 없으면 영원히 진행하지 못한다. 예산을 올려 잡는다.
        const std::uint32_t frameBytes = m_config.maxMessageBytes + MessageHeaderBytes + WebSocket::MaxFrameHeaderBytes;
        m_config.sendBufferBytes = Max(m_config.sendBufferBytes, frameBytes);
        m_config.receiveBufferBytes = Max(m_config.receiveBufferBytes, frameBytes);
        m_connections.Reserve(m_config.maxConnections);
        m_inbound.Resize(m_config.inboundBytes);
        m_records.Resize(m_config.inboundMessages);
        m_events.Resize(m_config.eventCapacity);
        m_scratch.Resize(Max(MaxHandshakeBytes, 4096));
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
        if (m_role != NetworkRole::None || nullptr == host)
        {
            return false;
        }
        const char* bareHost = StripScheme(host);
        if (std::strlen(bareHost) >= Connection::HostCapacity)
        {
            return false;
        }
        OwnerPtr<IStreamSocket> stream = m_provider.CreateStreamSocket();
        if (nullptr == stream.Get())
        {
            return false;
        }
        if (false == stream->Connect(bareHost, port))
        {
            return false;
        }
        m_role = NetworkRole::Client;
        Connection& connection = AddConnection(ServerConnectionId, std::move(stream), false);
        std::memcpy(connection.host, bareHost, std::strlen(bareHost) + 1);
        connection.port = port;
        return true;
    }

    void Transport::Close()
    {
        for (Connection& connection : m_connections)
        {
            if (connection.phase == Phase::Ready)
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
        if (nullptr == connection || connection->wantsClose)
        {
            return ConnectionState::Disconnected;
        }
        return connection->phase == Phase::Ready ? ConnectionState::Connected : ConnectionState::Connecting;
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

    double Transport::GetRoundTripMilliseconds(ConnectionId id) const
    {
        const Connection* connection = FindConnection(id);
        if (nullptr == connection)
        {
            return -1.0;
        }
        return connection->roundTripMilliseconds;
    }

    const TransportConfig& Transport::GetConfig() const
    {
        return m_config;
    }

    void Transport::SetFragmentBytesForTests(std::uint32_t bytes)
    {
        m_fragmentBytesForTests = bytes;
    }

    // ── 송신 ────────────────────────────────────────────────────────────────────────────────────

    bool Transport::Send(ConnectionId id, MessageId messageId, const void* data, std::uint32_t size, NetChannel channel)
    {
        if (size > m_config.maxMessageBytes || false == IsKnownChannel(channel) || messageId >= FirstSystemMessageId)
        {
            return false;
        }
        if (size > 0 && nullptr == data)
        {
            return false;
        }
        Connection* connection = FindConnection(id);
        if (nullptr == connection || connection->phase != Phase::Ready || connection->wantsClose)
        {
            return false;
        }
        // 2 단계에서는 전 채널이 WS 다. 3 단계가 비신뢰·무순서 채널을 UDP 로 돌린다.
        if (false == QueueMessage(*connection, messageId, data, size))
        {
            return false;
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

    bool Transport::QueueMessage(Connection& connection, MessageId messageId, const void* data, std::uint32_t size)
    {
        std::uint8_t header[MessageHeaderBytes];
        header[0] = static_cast<std::uint8_t>(messageId & 0xFF);
        header[1] = static_cast<std::uint8_t>((messageId >> 8) & 0xFF);
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
        const std::uint32_t chunk = m_fragmentBytesForTests;
        if (0 == chunk || chunk >= MessageHeaderBytes + size)
        {
            return QueueFrame(connection, WebSocket::Opcode::Binary, true, header, MessageHeaderBytes, bytes, size);
        }
        // 테스트용 조각내기. 전부 들어갈 자리가 있는지 먼저 본다 - 절반만 보내면 상대가 영원히 기다린다.
        const std::uint32_t total = MessageHeaderBytes + size;
        const std::uint32_t frames = (total + chunk - 1) / chunk;
        if (connection.send.Free() < total + frames * WebSocket::MaxFrameHeaderBytes)
        {
            return false;
        }
        std::uint8_t staging[MessageHeaderBytes];
        std::memcpy(staging, header, MessageHeaderBytes);
        std::uint32_t position = 0;
        bool first = true;
        while (position < total)
        {
            const std::uint32_t frameSize = (total - position < chunk) ? (total - position) : chunk;
            const bool fin = position + frameSize >= total;
            // 이 조각이 헤더 바이트와 데이터 바이트 어디에 걸치는지 나눈다.
            std::uint32_t headerPart = 0;
            if (position < MessageHeaderBytes)
            {
                headerPart = (MessageHeaderBytes - position < frameSize) ? (MessageHeaderBytes - position) : frameSize;
            }
            const std::uint32_t dataStart = position + headerPart - MessageHeaderBytes;
            const std::uint32_t dataPart = frameSize - headerPart;
            QueueFrame(connection, first ? WebSocket::Opcode::Binary : WebSocket::Opcode::Continuation, fin,
                staging + position, headerPart, bytes + (headerPart == frameSize ? 0 : dataStart), dataPart);
            position += frameSize;
            first = false;
        }
        return true;
    }

    bool Transport::QueueFrame(Connection& connection, WebSocket::Opcode opcode, bool fin, const std::uint8_t* first,
        std::uint32_t firstSize, const std::uint8_t* second, std::uint32_t secondSize)
    {
        // 클라이언트 → 서버는 마스크, 서버 → 클라이언트는 마스크 없음(RFC6455 §5.3).
        const bool mask = false == connection.serverSide;
        const std::uint32_t maskKey = mask ? NextMaskKey() : 0;
        std::uint8_t header[WebSocket::MaxFrameHeaderBytes];
        const std::uint32_t headerLength = WebSocket::EncodeFrameHeader(
            opcode, fin, static_cast<std::uint64_t>(firstSize) + secondSize, mask, maskKey, header);
        if (connection.send.Free() < headerLength + firstSize + secondSize)
        {
            return false;
        }
        connection.send.Write(header, headerLength);
        const std::uint8_t maskBytes[4] = {
            static_cast<std::uint8_t>((maskKey >> 24) & 0xFF), static_cast<std::uint8_t>((maskKey >> 16) & 0xFF),
            static_cast<std::uint8_t>((maskKey >> 8) & 0xFF), static_cast<std::uint8_t>(maskKey & 0xFF) };
        std::uint32_t maskOffset = 0;
        WriteMaskedRun(connection, first, firstSize, mask, maskBytes, maskOffset);
        WriteMaskedRun(connection, second, secondSize, mask, maskBytes, maskOffset);
        return true;
    }

    void Transport::WriteMaskedRun(Connection& connection, const std::uint8_t* data, std::uint32_t size, bool mask,
        const std::uint8_t maskBytes[4], std::uint32_t& maskOffset)
    {
        if (0 == size)
        {
            return;
        }
        if (false == mask)
        {
            connection.send.Write(data, size);
            return;
        }
        std::uint32_t position = 0;
        while (position < size)
        {
            const std::uint32_t run = (size - position < m_scratch.Size()) ? (size - position)
                : static_cast<std::uint32_t>(m_scratch.Size());
            std::memcpy(m_scratch.Data(), data + position, run);
            WebSocket::ApplyMask(m_scratch.Data(), run, maskBytes, maskOffset);
            connection.send.Write(m_scratch.Data(), run);
            position += run;
            maskOffset += run;
        }
    }

    template <typename T>
    void Transport::SendSystem(Connection& connection, MessageId messageId, const T& payload)
    {
        // 세션 제어는 항상 신뢰 WS 다. 자리가 없으면 다음 ping 때 다시 시도되는 성질의 것들이라 실패를 삼킨다.
        if (QueueMessage(connection, messageId, &payload, sizeof(T)))
        {
            FlushSend(connection);
        }
    }

    std::uint32_t Transport::NextMaskKey()
    {
        std::uint32_t x = m_maskState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_maskState = x;
        return x;
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
        UpdateSessions();
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
            Connection& connection = AddConnection(m_nextClientId++, std::move(accepted), true);
            // 서버 쪽은 소켓이 이미 붙어 있다. 클라이언트의 업그레이드 요청을 기다린다.
            connection.phase = Phase::WebSocketHandshaking;
        }
    }

    void Transport::PollConnection(Connection& connection)
    {
        if (connection.wantsClose || nullptr == connection.stream.Get())
        {
            return;
        }
        if (connection.phase == Phase::TcpConnecting)
        {
            const ConnectionState socketState = connection.stream->GetState();
            if (socketState == ConnectionState::Disconnected)
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            if (socketState != ConnectionState::Connected)
            {
                return;
            }
            BeginWebSocketHandshake(connection);
        }
        FlushSend(connection);
        if (connection.wantsClose)
        {
            return;
        }
        ReadIntoRing(connection);
        if (connection.wantsClose)
        {
            return;
        }
        if (connection.phase == Phase::WebSocketHandshaking)
        {
            PumpWebSocketHandshake(connection);
        }
        if (connection.phase == Phase::SessionHandshaking || connection.phase == Phase::Ready)
        {
            PumpFrames(connection);
        }
        FlushSend(connection);
    }

    void Transport::BeginWebSocketHandshake(Connection& connection)
    {
        connection.phase = Phase::WebSocketHandshaking;
        if (connection.serverSide)
        {
            return;
        }
        WebSocket::GenerateClientKey((static_cast<std::uint64_t>(connection.id) << 32) ^ NextMaskKey(), connection.clientKey);
        char* request = reinterpret_cast<char*>(m_scratch.Data());
        const std::uint32_t length = WebSocket::BuildClientHandshakeRequest(
            connection.host, connection.port, connection.clientKey, request, static_cast<std::uint32_t>(m_scratch.Size()));
        if (0 == length || false == connection.send.Write(request, length))
        {
            RequestClose(connection, DisconnectReason::Error);
        }
    }

    void Transport::PumpWebSocketHandshake(Connection& connection)
    {
        const std::uint32_t available = connection.receive.Size() < MaxHandshakeBytes ? connection.receive.Size() : MaxHandshakeBytes;
        if (0 == available)
        {
            return;
        }
        connection.receive.Peek(m_scratch.Data(), available);
        std::uint32_t consumed = 0;
        WebSocket::ParseResult result = WebSocket::ParseResult::Invalid;
        if (connection.serverSide)
        {
            WebSocket::ServerHandshakeRequest request;
            result = WebSocket::ParseServerHandshake(m_scratch.Data(), available, consumed, request);
            if (result == WebSocket::ParseResult::Ok)
            {
                char* response = reinterpret_cast<char*>(m_scratch.Data());
                const std::uint32_t length = WebSocket::BuildServerHandshakeResponse(
                    request, response, static_cast<std::uint32_t>(m_scratch.Size()));
                if (0 == length || false == connection.send.Write(response, length))
                {
                    RequestClose(connection, DisconnectReason::Error);
                    return;
                }
            }
        }
        else
        {
            char expected[WebSocket::AcceptKeyChars + 1];
            WebSocket::ComputeAcceptKey(connection.clientKey, WebSocket::ClientKeyChars, expected);
            result = WebSocket::ParseClientHandshakeResponse(m_scratch.Data(), available, consumed, expected);
        }
        if (result == WebSocket::ParseResult::NeedMoreData)
        {
            if (available >= MaxHandshakeBytes)
            {
                RequestClose(connection, DisconnectReason::Error);
            }
            return;
        }
        if (result == WebSocket::ParseResult::Invalid)
        {
            RequestClose(connection, DisconnectReason::Error);
            return;
        }
        connection.receive.Discard(consumed);
        connection.phase = Phase::SessionHandshaking;
        // 클라이언트가 hello 를 먼저 보낸다. 서버는 받아서 검증하고 응답한다.
        if (false == connection.serverSide)
        {
            HelloPayload hello;
            hello.protocolVersion = m_config.protocolVersion;
            SendSystem(connection, SystemHello, hello);
        }
    }

    void Transport::PumpFrames(Connection& connection)
    {
        std::uint8_t headerBytes[WebSocket::MaxFrameHeaderBytes];
        while (false == connection.wantsClose)
        {
            const std::uint32_t peeked = connection.receive.Peek(headerBytes, WebSocket::MaxFrameHeaderBytes);
            WebSocket::FrameHeader header;
            const WebSocket::ParseResult result = WebSocket::DecodeFrameHeader(headerBytes, peeked, header);
            if (result == WebSocket::ParseResult::NeedMoreData)
            {
                return;
            }
            if (result == WebSocket::ParseResult::Invalid)
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            const std::uint64_t total = header.headerLength + header.payloadLength;
            if (header.payloadLength > m_config.maxMessageBytes + MessageHeaderBytes || total > connection.receive.Capacity())
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            if (connection.receive.Size() < total)
            {
                return;
            }
            const std::uint32_t payloadLength = static_cast<std::uint32_t>(header.payloadLength);
            switch (header.opcode)
            {
            case WebSocket::Opcode::Close:
            {
                // 되돌려 보내고 닫는다.
                QueueFrame(connection, WebSocket::Opcode::Close, true, nullptr, 0, nullptr, 0);
                RequestClose(connection, DisconnectReason::Normal);
                return;
            }
            case WebSocket::Opcode::Ping:
            {
                if (payloadLength > WebSocket::MaxControlPayloadBytes)
                {
                    RequestClose(connection, DisconnectReason::Error);
                    return;
                }
                connection.receive.PeekAt(header.headerLength, m_scratch.Data(), payloadLength);
                if (header.masked)
                {
                    WebSocket::ApplyMask(m_scratch.Data(), payloadLength, header.mask, 0);
                }
                QueueFrame(connection, WebSocket::Opcode::Pong, true, m_scratch.Data(), payloadLength, nullptr, 0);
                connection.receive.Discard(static_cast<std::uint32_t>(total));
                break;
            }
            case WebSocket::Opcode::Pong:
            {
                connection.receive.Discard(static_cast<std::uint32_t>(total));
                break;
            }
            case WebSocket::Opcode::Binary:
            case WebSocket::Opcode::Text:
            case WebSocket::Opcode::Continuation:
            {
                if (false == DeliverDataFrame(connection, header))
                {
                    return;
                }
                break;
            }
            default:
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            }
        }
    }

    bool Transport::DeliverDataFrame(Connection& connection, const WebSocket::FrameHeader& header)
    {
        const std::uint32_t payloadLength = static_cast<std::uint32_t>(header.payloadLength);
        const std::uint32_t total = header.headerLength + payloadLength;
        const bool isStart = header.opcode != WebSocket::Opcode::Continuation;
        if (isStart && connection.inFragment)
        {
            // 앞 조각이 끝나지 않았는데 새 메시지가 시작됐다.
            RequestClose(connection, DisconnectReason::Error);
            return false;
        }
        if (false == isStart && false == connection.inFragment)
        {
            RequestClose(connection, DisconnectReason::Error);
            return false;
        }
        if (isStart && header.fin)
        {
            // 흔한 길: 프레임 하나가 메시지 하나다. 고리에서 바로 꺼내 전달한다.
            if (payloadLength < MessageHeaderBytes)
            {
                RequestClose(connection, DisconnectReason::Error);
                return false;
            }
            std::uint8_t idBytes[MessageHeaderBytes];
            connection.receive.PeekAt(header.headerLength, idBytes, MessageHeaderBytes);
            if (header.masked)
            {
                WebSocket::ApplyMask(idBytes, MessageHeaderBytes, header.mask, 0);
            }
            const MessageId messageId = ReadMessageId(idBytes);
            const std::uint32_t bodySize = payloadLength - MessageHeaderBytes;
            if (messageId >= FirstSystemMessageId)
            {
                const std::uint32_t copy = bodySize < m_scratch.Size() ? bodySize : static_cast<std::uint32_t>(m_scratch.Size());
                connection.receive.PeekAt(header.headerLength + MessageHeaderBytes, m_scratch.Data(), copy);
                if (header.masked)
                {
                    WebSocket::ApplyMask(m_scratch.Data(), copy, header.mask, MessageHeaderBytes);
                }
                connection.receive.Discard(total);
                HandleSystemMessage(connection, messageId, m_scratch.Data(), copy);
                return true;
            }
            std::uint8_t* destination = ReserveInbound(connection, messageId, bodySize);
            if (nullptr == destination)
            {
                return false;
            }
            if (bodySize > 0)
            {
                connection.receive.PeekAt(header.headerLength + MessageHeaderBytes, destination, bodySize);
                if (header.masked)
                {
                    WebSocket::ApplyMask(destination, bodySize, header.mask, MessageHeaderBytes);
                }
            }
            connection.receive.Discard(total);
            return true;
        }
        // 조각난 메시지. 조립 버퍼에 모아 마지막 조각에서 전달한다.
        if (connection.fragmentSize + payloadLength > connection.fragment.Size())
        {
            RequestClose(connection, DisconnectReason::Error);
            return false;
        }
        std::uint8_t* at = connection.fragment.Data() + connection.fragmentSize;
        connection.receive.PeekAt(header.headerLength, at, payloadLength);
        if (header.masked)
        {
            WebSocket::ApplyMask(at, payloadLength, header.mask, 0);
        }
        if (false == header.fin)
        {
            connection.fragmentSize += payloadLength;
            connection.inFragment = true;
            connection.receive.Discard(total);
            return true;
        }
        const std::uint32_t assembled = connection.fragmentSize + payloadLength;
        if (false == DeliverMessage(connection, connection.fragment.Data(), assembled))
        {
            // 저장소가 없다. 조립 버퍼는 그대로 두고 이 프레임은 다음에 다시 본다.
            return false;
        }
        connection.fragmentSize = 0;
        connection.inFragment = false;
        connection.receive.Discard(total);
        return true;
    }

    bool Transport::DeliverMessage(Connection& connection, const std::uint8_t* message, std::uint32_t size)
    {
        if (size < MessageHeaderBytes)
        {
            RequestClose(connection, DisconnectReason::Error);
            return true;
        }
        const MessageId messageId = ReadMessageId(message);
        const std::uint8_t* body = message + MessageHeaderBytes;
        const std::uint32_t bodySize = size - MessageHeaderBytes;
        if (messageId >= FirstSystemMessageId)
        {
            HandleSystemMessage(connection, messageId, body, bodySize);
            return true;
        }
        std::uint8_t* destination = ReserveInbound(connection, messageId, bodySize);
        if (nullptr == destination)
        {
            return false;
        }
        if (bodySize > 0)
        {
            std::memcpy(destination, body, bodySize);
        }
        return true;
    }

    void Transport::HandleSystemMessage(Connection& connection, MessageId messageId, const std::uint8_t* payload, std::uint32_t size)
    {
        switch (messageId)
        {
        case SystemHello:
        {
            if (false == connection.serverSide || sizeof(HelloPayload) != size)
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            HelloPayload hello;
            std::memcpy(&hello, payload, sizeof(hello));
            if (hello.protocolVersion != m_config.protocolVersion)
            {
                // 거부 사유를 보내되 **여기서 닫지 않는다.** 보낼 것이 남은 쪽이 먼저 닫으면 RST 가 버퍼된 Bye 를 삼킨다.
                // 클라이언트가 Bye 를 읽고 스스로 닫거나, 핸드셰이크 타임아웃이 정리한다.
                ByePayload bye;
                bye.reason = static_cast<std::uint8_t>(DisconnectReason::VersionMismatch);
                SendSystem(connection, SystemBye, bye);
                return;
            }
            HelloPayload ack;
            ack.protocolVersion = m_config.protocolVersion;
            SendSystem(connection, SystemHelloAck, ack);
            PromoteToReady(connection);
            return;
        }
        case SystemHelloAck:
        {
            if (connection.serverSide)
            {
                RequestClose(connection, DisconnectReason::Error);
                return;
            }
            PromoteToReady(connection);
            return;
        }
        case SystemBye:
        {
            DisconnectReason reason = DisconnectReason::Normal;
            if (sizeof(ByePayload) == size)
            {
                reason = static_cast<DisconnectReason>(payload[0]);
            }
            RequestClose(connection, reason);
            return;
        }
        case SystemPing:
        {
            if (sizeof(PingPayload) == size)
            {
                PingPayload ping;
                std::memcpy(&ping, payload, sizeof(ping));
                // 상대의 시각을 그대로 되돌린다. 상대가 자기 시계로 뺀다.
                SendSystem(connection, SystemPong, ping);
            }
            return;
        }
        case SystemPong:
        {
            if (sizeof(PingPayload) == size)
            {
                PingPayload pong;
                std::memcpy(&pong, payload, sizeof(pong));
                connection.roundTripMilliseconds = m_clock.NowMilliseconds() - pong.sendMilliseconds;
            }
            return;
        }
        default:
        {
            // 모르는 시스템 메시지는 무시한다 - 앞으로의 호환을 위해서다.
            return;
        }
        }
    }

    void Transport::PromoteToReady(Connection& connection)
    {
        if (connection.phase == Phase::Ready)
        {
            return;
        }
        connection.phase = Phase::Ready;
        const double now = m_clock.NowMilliseconds();
        connection.lastReceiveMilliseconds = now;
        connection.lastPingMilliseconds = now;
        PushEvent(NetworkEventKind::Connected, connection.id, DisconnectReason::Normal);
    }

    void Transport::UpdateSessions()
    {
        const double now = m_clock.NowMilliseconds();
        for (Connection& connection : m_connections)
        {
            if (connection.wantsClose)
            {
                continue;
            }
            // 무응답은 어느 단계든 같은 시한이다. 접속 중, 핸드셰이크 중, 준비 뒤 전부.
            if (now - connection.lastReceiveMilliseconds > m_config.timeoutMilliseconds)
            {
                RequestClose(connection, DisconnectReason::Timeout);
                continue;
            }
            if (connection.phase == Phase::Ready && now - connection.lastPingMilliseconds >= m_config.pingIntervalMilliseconds)
            {
                connection.lastPingMilliseconds = now;
                PingPayload ping;
                ping.sendMilliseconds = now;
                SendSystem(connection, SystemPing, ping);
            }
        }
    }

    void Transport::FlushSend(Connection& connection)
    {
        while (false == connection.send.IsEmpty() && false == connection.wantsClose)
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
        bool gotAny = false;
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
                gotAny = true;
                continue;
            }
            if (io == SocketIo::WouldBlock)
            {
                break;
            }
            RequestClose(connection, io == SocketIo::Closed ? DisconnectReason::Normal : DisconnectReason::Error);
            break;
        }
        if (gotAny)
        {
            connection.lastReceiveMilliseconds = m_clock.NowMilliseconds();
        }
    }

    std::uint8_t* Transport::ReserveInbound(const Connection& connection, MessageId messageId, std::uint32_t size)
    {
        if (m_recordCount >= m_records.Size())
        {
            return nullptr;
        }
        if (m_inbound.Size() - m_inboundSize < size)
        {
            return nullptr;
        }
        InboundRecord& record = m_records[m_recordCount];
        record.connection = connection.id;
        record.offset = m_inboundSize;
        record.size = size;
        record.messageId = messageId;
        record.channel = NetChannel::ReliableOrdered;
        std::uint8_t* destination = m_inbound.Data() + m_inboundSize;
        m_inboundSize += size;
        ++m_recordCount;
        return destination;
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
            // 알릴 대상: 준비됐던 연결(게임이 Connected 를 받았다), 그리고 클라이언트의 시도(실패 이유를 알아야 한다).
            // 서버가 받아들였다가 hello 전에 떨어진 것은 게임에 보이지 않는다 - 스캐너와 버전 불일치가 그것이다.
            if (connection.phase == Phase::Ready || false == connection.serverSide)
            {
                PushEvent(NetworkEventKind::Disconnected, connection.id, connection.closeReason);
            }
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

    Transport::Connection& Transport::AddConnection(ConnectionId id, OwnerPtr<IStreamSocket> stream, bool serverSide)
    {
        Connection& connection = m_connections.Emplace();
        connection.id = id;
        connection.stream = std::move(stream);
        connection.serverSide = serverSide;
        connection.phase = Phase::TcpConnecting;
        connection.send.Reset(m_config.sendBufferBytes);
        connection.receive.Reset(m_config.receiveBufferBytes);
        connection.fragment.Resize(m_config.maxMessageBytes + MessageHeaderBytes);
        const double now = m_clock.NowMilliseconds();
        connection.lastReceiveMilliseconds = now;
        connection.lastPingMilliseconds = now;
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
