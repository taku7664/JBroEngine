#include <JBro/Network/Testing/MemorySocketProvider.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace JBro::Network::Testing
{
    // ── provider ────────────────────────────────────────────────────────────────────────────────

    MemorySocketProvider::MemorySocketProvider(std::uint32_t pipeBytes, std::uint32_t datagramQueueLength)
        : m_pipeBytes(pipeBytes)
        , m_datagramQueueLength(datagramQueueLength)
    {
    }

    MemorySocketProvider::~MemorySocketProvider() = default;

    OwnerPtr<IStreamSocket> MemorySocketProvider::CreateStreamSocket()
    {
        SweepPipes();
        return MakeOwnerPtr<MemoryStreamSocket>(*this);
    }

    OwnerPtr<IDatagramSocket> MemorySocketProvider::CreateDatagramSocket()
    {
        if (false == m_datagramAvailable)
        {
            return nullptr;
        }
        OwnerPtr<IDatagramSocket> socket = MakeOwnerPtr<MemoryDatagramSocket>(*this);
        if (false == m_lossy)
        {
            return socket;
        }
        OwnerPtr<LossyDatagramSocket> lossy = MakeOwnerPtr<LossyDatagramSocket>(std::move(socket), m_lossyConfig);
        m_lossySockets.Add(lossy.Get());
        return lossy;
    }

    void MemorySocketProvider::SetLossy(const LossyConfig& config)
    {
        m_lossy = true;
        m_lossyConfig = config;
    }

    void MemorySocketProvider::ClearLossy()
    {
        m_lossy = false;
    }

    LossyDatagramSocket* MemorySocketProvider::GetLossySocketAt(std::uint32_t index) const
    {
        if (index >= m_lossySockets.Size())
        {
            return nullptr;
        }
        return m_lossySockets[index];
    }

    OwnerPtr<IPeerConnection> MemorySocketProvider::CreatePeerConnection(const PeerConnectionDesc& desc)
    {
        if (false == m_peerAvailable)
        {
            return nullptr;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(m_peers.Size());
        OwnerPtr<MemoryPeerConnection> peer = MakeOwnerPtr<MemoryPeerConnection>(*this, index, desc.initiator);
        m_peers.Add(peer.Get());
        return peer;
    }

    void MemorySocketProvider::SetPeerAvailable(bool available)
    {
        m_peerAvailable = available;
    }

    MemorySocketProvider::PeerLink* MemorySocketProvider::LinkPeers(std::uint32_t initiatorIndex, MemoryPeerConnection* acceptor)
    {
        if (initiatorIndex >= m_peers.Size() || nullptr == m_peers[initiatorIndex] || nullptr == acceptor)
        {
            return nullptr;
        }
        OwnerPtr<PeerLink> owner = MakeOwnerPtr<PeerLink>();
        for (std::uint32_t channel = 0; channel < NetChannelCount; ++channel)
        {
            owner->toAcceptor[channel].Reset(m_pipeBytes);
            owner->toInitiator[channel].Reset(m_pipeBytes);
        }
        PeerLink* link = owner.Get();
        m_peerLinks.Add(std::move(owner));
        m_peers[initiatorIndex]->AttachLink(link);
        acceptor->AttachLink(link);
        return link;
    }

    void MemorySocketProvider::UnregisterPeer(MemoryPeerConnection* peer)
    {
        for (MemoryPeerConnection*& slot : m_peers)
        {
            if (slot == peer)
            {
                slot = nullptr;
            }
        }
    }

    bool MemorySocketProvider::ShouldDropUnreliable(PeerLink& link)
    {
        if (false == m_lossy || m_lossyConfig.lossRate <= 0.0)
        {
            return false;
        }
        std::uint32_t x = link.rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        link.rng = x;
        return static_cast<double>(x >> 8) * (1.0 / 16777216.0) < m_lossyConfig.lossRate;
    }

    void MemorySocketProvider::SetDatagramAvailable(bool available)
    {
        m_datagramAvailable = available;
    }

    bool MemorySocketProvider::RegisterListener(std::uint16_t port)
    {
        for (const Listener& listener : m_listeners)
        {
            if (listener.port == port)
            {
                return false;
            }
        }
        Listener& listener = m_listeners.Emplace();
        listener.port = port;
        return true;
    }

    void MemorySocketProvider::UnregisterListener(std::uint16_t port)
    {
        for (std::size_t index = 0; index < m_listeners.Size(); ++index)
        {
            if (m_listeners[index].port != port)
            {
                continue;
            }
            // 아직 받지 않은 접속은 거부된 것으로 본다.
            for (Pipe* pipe : m_listeners[index].pending)
            {
                pipe->acceptorOpen = false;
            }
            m_listeners.RemoveAt(index);
            return;
        }
    }

    MemorySocketProvider::Pipe* MemorySocketProvider::ConnectTo(std::uint16_t port)
    {
        for (Listener& listener : m_listeners)
        {
            if (listener.port != port)
            {
                continue;
            }
            OwnerPtr<Pipe> owner = MakeOwnerPtr<Pipe>();
            owner->toAcceptor.Reset(m_pipeBytes);
            owner->toConnector.Reset(m_pipeBytes);
            Pipe* pipe = owner.Get();
            m_pipes.Add(std::move(owner));
            listener.pending.Add(pipe);
            return pipe;
        }
        return nullptr;
    }

    MemorySocketProvider::Pipe* MemorySocketProvider::TakePendingConnection(std::uint16_t port)
    {
        for (Listener& listener : m_listeners)
        {
            if (listener.port != port || listener.pending.IsEmpty())
            {
                continue;
            }
            Pipe* pipe = listener.pending[0];
            listener.pending.RemoveAt(0);
            pipe->accepted = true;
            return pipe;
        }
        return nullptr;
    }

    void MemorySocketProvider::ReleasePipeSide(Pipe* pipe, bool connector)
    {
        if (nullptr == pipe)
        {
            return;
        }
        if (connector)
        {
            pipe->connectorOpen = false;
        }
        else
        {
            pipe->acceptorOpen = false;
        }
    }

    void MemorySocketProvider::SweepPipes()
    {
        std::size_t index = 0;
        while (index < m_pipes.Size())
        {
            Pipe* pipe = m_pipes[index].Get();
            const bool bothClosed = false == pipe->connectorOpen && false == pipe->acceptorOpen;
            bool pending = false;
            for (const Listener& listener : m_listeners)
            {
                if (listener.pending.Contains(pipe))
                {
                    pending = true;
                }
            }
            if (bothClosed && false == pending)
            {
                m_pipes.RemoveAt(index);
                continue;
            }
            ++index;
        }
    }

    bool MemorySocketProvider::BindDatagram(std::uint16_t& port, MemoryDatagramSocket* socket)
    {
        if (0 == port)
        {
            port = m_nextEphemeralPort++;
        }
        for (const DatagramBinding& binding : m_datagramBindings)
        {
            if (binding.port == port)
            {
                return false;
            }
        }
        DatagramBinding& binding = m_datagramBindings.Emplace();
        binding.port = port;
        binding.socket = socket;
        return true;
    }

    void MemorySocketProvider::UnbindDatagram(std::uint16_t port)
    {
        for (std::size_t index = 0; index < m_datagramBindings.Size(); ++index)
        {
            if (m_datagramBindings[index].port == port)
            {
                m_datagramBindings.RemoveAt(index);
                return;
            }
        }
    }

    bool MemorySocketProvider::DeliverDatagram(const Endpoint& to, const Endpoint& from, const void* data, std::size_t size)
    {
        const std::uint16_t port = PortOf(to);
        for (const DatagramBinding& binding : m_datagramBindings)
        {
            if (binding.port == port)
            {
                return binding.socket->Enqueue(from, data, size);
            }
        }
        // 받는 이가 없는 데이터그램은 조용히 사라진다.
        return false;
    }

    Endpoint MemorySocketProvider::MakeEndpoint(std::uint16_t port)
    {
        Endpoint endpoint;
        endpoint.data[0] = static_cast<std::uint8_t>(port & 0xFF);
        endpoint.data[1] = static_cast<std::uint8_t>((port >> 8) & 0xFF);
        endpoint.length = 2;
        return endpoint;
    }

    std::uint16_t MemorySocketProvider::PortOf(const Endpoint& endpoint)
    {
        if (endpoint.length < 2)
        {
            return 0;
        }
        return static_cast<std::uint16_t>(endpoint.data[0] | (endpoint.data[1] << 8));
    }

    std::uint32_t MemorySocketProvider::GetDatagramQueueLength() const
    {
        return m_datagramQueueLength;
    }

    // ── 스트림 소켓 ─────────────────────────────────────────────────────────────────────────────

    MemoryStreamSocket::MemoryStreamSocket(MemorySocketProvider& provider)
        : m_provider(provider)
    {
    }

    MemoryStreamSocket::MemoryStreamSocket(MemorySocketProvider& provider, MemorySocketProvider::Pipe* pipe, bool connector)
        : m_provider(provider)
        , m_pipe(pipe)
        , m_connector(connector)
    {
    }

    MemoryStreamSocket::~MemoryStreamSocket()
    {
        Close();
    }

    bool MemoryStreamSocket::Connect(const char* host, std::uint16_t port)
    {
        (void)host;
        if (m_closed || m_listening || nullptr != m_pipe)
        {
            return false;
        }
        m_pipe = m_provider.ConnectTo(port);
        if (nullptr == m_pipe)
        {
            return false;
        }
        m_connector = true;
        return true;
    }

    bool MemoryStreamSocket::Listen(std::uint16_t port)
    {
        if (m_closed || nullptr != m_pipe)
        {
            return false;
        }
        if (false == m_provider.RegisterListener(port))
        {
            return false;
        }
        m_listening = true;
        m_port = port;
        return true;
    }

    OwnerPtr<IStreamSocket> MemoryStreamSocket::Accept()
    {
        if (false == m_listening || m_closed)
        {
            return nullptr;
        }
        MemorySocketProvider::Pipe* pipe = m_provider.TakePendingConnection(m_port);
        if (nullptr == pipe)
        {
            return nullptr;
        }
        return MakeOwnerPtr<MemoryStreamSocket>(m_provider, pipe, false);
    }

    ConnectionState MemoryStreamSocket::GetState() const
    {
        if (m_closed || nullptr == m_pipe)
        {
            return ConnectionState::Disconnected;
        }
        if (false == PeerOpen())
        {
            return ConnectionState::Disconnected;
        }
        if (m_connector && false == m_pipe->accepted)
        {
            return ConnectionState::Connecting;
        }
        return ConnectionState::Connected;
    }

    SocketIo MemoryStreamSocket::Send(const void* data, std::size_t size, std::size_t& outSent)
    {
        outSent = 0;
        if (m_closed || nullptr == m_pipe)
        {
            return SocketIo::Error;
        }
        if (false == PeerOpen())
        {
            return SocketIo::Closed;
        }
        if (GetState() != ConnectionState::Connected)
        {
            return SocketIo::WouldBlock;
        }
        ByteRing& out = Outgoing();
        const std::uint32_t room = out.Free();
        if (0 == room)
        {
            return SocketIo::WouldBlock;
        }
        const std::uint32_t count = size < room ? static_cast<std::uint32_t>(size) : room;
        out.Write(data, count);
        outSent = count;
        return SocketIo::Ok;
    }

    SocketIo MemoryStreamSocket::Receive(void* buffer, std::size_t capacity, std::size_t& outReceived)
    {
        outReceived = 0;
        if (m_closed || nullptr == m_pipe)
        {
            return SocketIo::Error;
        }
        ByteRing& in = Incoming();
        const std::uint32_t count = in.Read(buffer, static_cast<std::uint32_t>(capacity));
        if (count > 0)
        {
            outReceived = count;
            return SocketIo::Ok;
        }
        if (false == PeerOpen())
        {
            return SocketIo::Closed;
        }
        return SocketIo::WouldBlock;
    }

    void MemoryStreamSocket::Close()
    {
        if (m_closed)
        {
            return;
        }
        m_closed = true;
        if (m_listening)
        {
            m_provider.UnregisterListener(m_port);
            m_listening = false;
        }
        m_provider.ReleasePipeSide(m_pipe, m_connector);
        m_pipe = nullptr;
    }

    ByteRing& MemoryStreamSocket::Outgoing() const
    {
        return m_connector ? m_pipe->toAcceptor : m_pipe->toConnector;
    }

    ByteRing& MemoryStreamSocket::Incoming() const
    {
        return m_connector ? m_pipe->toConnector : m_pipe->toAcceptor;
    }

    bool MemoryStreamSocket::PeerOpen() const
    {
        return m_connector ? m_pipe->acceptorOpen : m_pipe->connectorOpen;
    }

    // ── 데이터그램 소켓 ─────────────────────────────────────────────────────────────────────────

    MemoryDatagramSocket::MemoryDatagramSocket(MemorySocketProvider& provider)
        : m_provider(provider)
    {
    }

    MemoryDatagramSocket::~MemoryDatagramSocket()
    {
        Close();
    }

    bool MemoryDatagramSocket::Open()
    {
        if (m_open)
        {
            return false;
        }
        m_queue.Resize(m_provider.GetDatagramQueueLength());
        m_open = true;
        return true;
    }

    bool MemoryDatagramSocket::Bind(std::uint16_t port)
    {
        if (false == m_open || m_bound)
        {
            return false;
        }
        std::uint16_t chosen = port;
        if (false == m_provider.BindDatagram(chosen, this))
        {
            return false;
        }
        m_port = chosen;
        m_bound = true;
        return true;
    }

    bool MemoryDatagramSocket::Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint)
    {
        (void)host;
        outEndpoint = MemorySocketProvider::MakeEndpoint(port);
        return true;
    }

    SocketIo MemoryDatagramSocket::SendTo(const Endpoint& to, const void* data, std::size_t size)
    {
        if (false == m_open)
        {
            return SocketIo::Error;
        }
        if (size > MemorySocketProvider::MaxDatagramBytes)
        {
            return SocketIo::Error;
        }
        if (false == m_bound && false == Bind(0))
        {
            return SocketIo::Error;
        }
        // 받는 이가 없거나 그쪽 큐가 찼어도 UDP 는 보낸 쪽에 알리지 않는다.
        m_provider.DeliverDatagram(to, MemorySocketProvider::MakeEndpoint(m_port), data, size);
        return SocketIo::Ok;
    }

    SocketIo MemoryDatagramSocket::ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom)
    {
        outReceived = 0;
        if (false == m_open)
        {
            return SocketIo::Error;
        }
        if (0 == m_count)
        {
            return SocketIo::WouldBlock;
        }
        const MemorySocketProvider::Datagram& datagram = m_queue[m_head];
        const std::size_t count = datagram.size < capacity ? datagram.size : capacity;
        std::memcpy(buffer, datagram.data, count);
        outReceived = count;
        outFrom = datagram.from;
        m_head = (m_head + 1) % static_cast<std::uint32_t>(m_queue.Size());
        --m_count;
        return SocketIo::Ok;
    }

    void MemoryDatagramSocket::Close()
    {
        if (m_bound)
        {
            m_provider.UnbindDatagram(m_port);
            m_bound = false;
        }
        m_open = false;
        m_count = 0;
        m_head = 0;
    }

    bool MemoryDatagramSocket::IsOpen() const
    {
        return m_open;
    }

    bool MemoryDatagramSocket::Enqueue(const Endpoint& from, const void* data, std::size_t size)
    {
        if (m_count >= m_queue.Size())
        {
            return false;
        }
        const std::uint32_t tail = (m_head + m_count) % static_cast<std::uint32_t>(m_queue.Size());
        MemorySocketProvider::Datagram& datagram = m_queue[tail];
        datagram.from = from;
        datagram.size = static_cast<std::uint32_t>(size);
        std::memcpy(datagram.data, data, size);
        ++m_count;
        return true;
    }
}

namespace JBro::Network::Testing
{
    // ── 피어 연결 ───────────────────────────────────────────────────────────────────────────────

    MemoryPeerConnection::MemoryPeerConnection(MemorySocketProvider& provider, std::uint32_t index, bool initiator)
        : m_provider(provider)
        , m_index(index)
        , m_initiator(initiator)
    {
    }

    MemoryPeerConnection::~MemoryPeerConnection()
    {
        Close();
        m_provider.UnregisterPeer(this);
    }

    void MemoryPeerConnection::AttachLink(MemorySocketProvider::PeerLink* link)
    {
        m_link = link;
    }

    std::uint32_t MemoryPeerConnection::Index() const
    {
        return m_index;
    }

    bool MemoryPeerConnection::PeerOpen() const
    {
        if (nullptr == m_link)
        {
            return false;
        }
        return m_initiator ? m_link->acceptorOpen : m_link->initiatorOpen;
    }

    ByteRing& MemoryPeerConnection::Outgoing(NetChannel channel) const
    {
        const std::uint32_t index = static_cast<std::uint32_t>(channel);
        return m_initiator ? m_link->toAcceptor[index] : m_link->toInitiator[index];
    }

    ByteRing& MemoryPeerConnection::Incoming(NetChannel channel) const
    {
        const std::uint32_t index = static_cast<std::uint32_t>(channel);
        return m_initiator ? m_link->toInitiator[index] : m_link->toAcceptor[index];
    }

    ConnectionState MemoryPeerConnection::GetState() const
    {
        if (m_closed)
        {
            return ConnectionState::Disconnected;
        }
        if (nullptr == m_link)
        {
            return ConnectionState::Connecting;
        }
        if (false == PeerOpen())
        {
            return ConnectionState::Disconnected;
        }
        return m_link->linked ? ConnectionState::Connected : ConnectionState::Connecting;
    }

    std::uint32_t MemoryPeerConnection::TakeSignal(void* buffer, std::uint32_t capacity)
    {
        if (m_closed)
        {
            return 0;
        }
        char text[16];
        int length = 0;
        if (m_initiator && m_stage == Stage::Idle)
        {
            length = std::snprintf(text, sizeof(text), "O%u", m_index);
            m_stage = Stage::WaitingAnswer;
        }
        else if (false == m_initiator && m_stage == Stage::AnswerReady)
        {
            length = std::snprintf(text, sizeof(text), "A%u", m_index);
            m_stage = Stage::Done;
        }
        if (length <= 0 || static_cast<std::uint32_t>(length) > capacity)
        {
            return 0;
        }
        std::memcpy(buffer, text, static_cast<std::size_t>(length));
        return static_cast<std::uint32_t>(length);
    }

    bool MemoryPeerConnection::PushSignal(const void* data, std::uint32_t size)
    {
        if (m_closed || size < 2 || size > 15)
        {
            return false;
        }
        char text[16] = {};
        std::memcpy(text, data, size);
        const std::uint32_t other = static_cast<std::uint32_t>(std::strtoul(text + 1, nullptr, 10));
        if (false == m_initiator && text[0] == 'O' && m_stage == Stage::Idle)
        {
            if (nullptr == m_provider.LinkPeers(other, this))
            {
                return false;
            }
            m_stage = Stage::AnswerReady;
            return true;
        }
        if (m_initiator && text[0] == 'A' && m_stage == Stage::WaitingAnswer && nullptr != m_link)
        {
            m_link->linked = true;
            m_stage = Stage::Done;
            return true;
        }
        return false;
    }

    SocketIo MemoryPeerConnection::Send(NetChannel channel, const void* data, std::size_t size)
    {
        if (GetState() != ConnectionState::Connected || static_cast<std::uint32_t>(channel) >= NetChannelCount)
        {
            return SocketIo::WouldBlock;
        }
        const bool unreliable = channel == NetChannel::Unreliable || channel == NetChannel::UnreliableSequenced;
        if (unreliable && m_provider.ShouldDropUnreliable(*m_link))
        {
            // 비신뢰 채널은 유실을 복구하지 않는다. 보낸 쪽은 모른다.
            return SocketIo::Ok;
        }
        ByteRing& ring = Outgoing(channel);
        const std::uint32_t recordSize = static_cast<std::uint32_t>(size);
        if (ring.Free() < 4 + recordSize)
        {
            return SocketIo::WouldBlock;
        }
        std::uint8_t header[4];
        header[0] = static_cast<std::uint8_t>(recordSize & 0xFF);
        header[1] = static_cast<std::uint8_t>((recordSize >> 8) & 0xFF);
        header[2] = static_cast<std::uint8_t>((recordSize >> 16) & 0xFF);
        header[3] = static_cast<std::uint8_t>((recordSize >> 24) & 0xFF);
        ring.Write(header, 4);
        if (recordSize > 0)
        {
            ring.Write(data, recordSize);
        }
        return SocketIo::Ok;
    }

    SocketIo MemoryPeerConnection::Receive(NetChannel& outChannel, void* buffer, std::size_t capacity, std::size_t& outReceived)
    {
        outReceived = 0;
        if (m_closed || nullptr == m_link)
        {
            return SocketIo::Error;
        }
        for (std::uint32_t index = 0; index < NetChannelCount; ++index)
        {
            const NetChannel channel = static_cast<NetChannel>(index);
            ByteRing& ring = Incoming(channel);
            if (ring.Size() < 4)
            {
                continue;
            }
            std::uint8_t header[4];
            ring.Peek(header, 4);
            const std::uint32_t recordSize = static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8)
                | (static_cast<std::uint32_t>(header[2]) << 16) | (static_cast<std::uint32_t>(header[3]) << 24);
            if (recordSize > capacity)
            {
                return SocketIo::Error;
            }
            ring.Discard(4);
            ring.Read(buffer, recordSize);
            outReceived = recordSize;
            outChannel = channel;
            return SocketIo::Ok;
        }
        if (false == PeerOpen())
        {
            return SocketIo::Closed;
        }
        return SocketIo::WouldBlock;
    }

    void MemoryPeerConnection::Close()
    {
        if (m_closed)
        {
            return;
        }
        m_closed = true;
        if (nullptr != m_link)
        {
            if (m_initiator)
            {
                m_link->initiatorOpen = false;
            }
            else
            {
                m_link->acceptorOpen = false;
            }
        }
    }
}
