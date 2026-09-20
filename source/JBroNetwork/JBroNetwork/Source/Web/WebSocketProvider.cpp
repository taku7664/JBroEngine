#include <JBro/Network/Web/WebSocketProvider.h>

// Emscripten 전용. Windows 빌드에서는 빈 번역 단위다. **미검증** - 웹 빌드가 서는 날 확인한다(network-plan §3-6).
#if defined(__EMSCRIPTEN__)

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Internal/WebSocketProtocol.h>

#include <emscripten.h>

#include <cstdio>
#include <cstring>

// ── JS 쪽 ──────────────────────────────────────────────────────────────────────────────────────
// 핸들 표 하나에 WebSocket 과 RTCPeerConnection 을 둔다. 받은 메시지는 JS 배열 큐에 쌓이고 C++ 이 폴링으로 꺼낸다 -
// 콜백을 C++ 로 넘기지 않는다(꺼내 가기 규약, network-plan §2.4).
EM_JS(int, jbro_ws_open, (const char* url), {
    var table = Module.jbroNet || (Module.jbroNet = { next: 1, items: {} });
    var id = table.next++;
    var entry = { ws: null, open: false, closed: false, queue: [] };
    try {
        var ws = new WebSocket(UTF8ToString(url));
        ws.binaryType = 'arraybuffer';
        ws.onopen = function() { entry.open = true; };
        ws.onclose = function() { entry.closed = true; entry.open = false; };
        ws.onerror = function() { entry.closed = true; entry.open = false; };
        ws.onmessage = function(event) { entry.queue.push(new Uint8Array(event.data)); };
        entry.ws = ws;
    } catch (error) {
        entry.closed = true;
    }
    table.items[id] = entry;
    return id;
});

EM_JS(int, jbro_ws_state, (int id), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || entry.closed) { return 2; }
    return entry.open ? 1 : 0;
});

EM_JS(int, jbro_ws_send, (int id, const unsigned char* data, int size), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || !entry.open) { return 0; }
    entry.ws.send(HEAPU8.slice(data, data + size));
    return 1;
});

// 큐 맨 앞 메시지를 최대 capacity 만큼 복사하고 크기를 돌려준다. 없으면 -1. 버퍼가 작으면 -2(메시지는 남는다).
EM_JS(int, jbro_ws_take, (int id, unsigned char* out, int capacity), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || entry.queue.length === 0) { return -1; }
    var message = entry.queue[0];
    if (message.length > capacity) { return -2; }
    HEAPU8.set(message, out);
    entry.queue.shift();
    return message.length;
});

EM_JS(void, jbro_ws_close, (int id), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry) { return; }
    try { if (entry.ws) { entry.ws.close(); } } catch (error) {}
    entry.closed = true;
    delete Module.jbroNet.items[id];
});

// 피어: 데이터 채널 넷을 채널 열거형 순서로 연다. 시그널은 JSON 글자({type:'offer'|'answer'|'candidate', ...}) 다.
EM_JS(int, jbro_peer_open, (int initiator, const char* iceServers), {
    var table = Module.jbroNet || (Module.jbroNet = { next: 1, items: {} });
    var id = table.next++;
    var servers = [];
    var text = UTF8ToString(iceServers);
    if (text.length > 0) { text.split('\u0001').forEach(function(url) { if (url.length > 0) { servers.push({ urls: url }); } }); }
    var entry = { pc: null, channels: [null, null, null, null], openCount: 0, closed: false, signals: [], queue: [] };
    try {
        var pc = new RTCPeerConnection({ iceServers: servers });
        entry.pc = pc;
        var options = [
            { ordered: true },
            { ordered: false },
            { ordered: false, maxRetransmits: 0 },
            { ordered: false, maxRetransmits: 0 }
        ];
        var wire = function(channel, index) {
            channel.binaryType = 'arraybuffer';
            channel.onopen = function() { entry.openCount++; };
            channel.onclose = function() { entry.closed = true; };
            channel.onmessage = function(event) { entry.queue.push({ channel: index, data: new Uint8Array(event.data) }); };
            entry.channels[index] = channel;
        };
        pc.onicecandidate = function(event) {
            if (event.candidate) { entry.signals.push(JSON.stringify({ type: 'candidate', candidate: event.candidate })); }
        };
        pc.onconnectionstatechange = function() {
            var state = pc.connectionState;
            if (state === 'failed' || state === 'closed' || state === 'disconnected') { entry.closed = true; }
        };
        if (initiator) {
            for (var index = 0; index < 4; ++index) {
                wire(pc.createDataChannel('jbro' + index, Object.assign({ negotiated: true, id: index }, options[index])), index);
            }
            pc.createOffer().then(function(offer) {
                return pc.setLocalDescription(offer).then(function() {
                    entry.signals.push(JSON.stringify({ type: 'offer', sdp: offer.sdp }));
                });
            }).catch(function() { entry.closed = true; });
        } else {
            for (var index = 0; index < 4; ++index) {
                wire(pc.createDataChannel('jbro' + index, Object.assign({ negotiated: true, id: index }, options[index])), index);
            }
        }
    } catch (error) {
        entry.closed = true;
    }
    table.items[id] = entry;
    return id;
});

EM_JS(int, jbro_peer_state, (int id), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || entry.closed) { return 2; }
    return entry.openCount >= 4 ? 1 : 0;
});

EM_JS(int, jbro_peer_take_signal, (int id, unsigned char* out, int capacity), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || entry.signals.length === 0) { return -1; }
    var bytes = new TextEncoder().encode(entry.signals[0]);
    if (bytes.length > capacity) { return -2; }
    HEAPU8.set(bytes, out);
    entry.signals.shift();
    return bytes.length;
});

EM_JS(int, jbro_peer_push_signal, (int id, const unsigned char* data, int size), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || !entry.pc) { return 0; }
    var message;
    try { message = JSON.parse(new TextDecoder().decode(HEAPU8.slice(data, data + size))); } catch (error) { return 0; }
    var pc = entry.pc;
    if (message.type === 'offer') {
        pc.setRemoteDescription({ type: 'offer', sdp: message.sdp }).then(function() {
            return pc.createAnswer();
        }).then(function(answer) {
            return pc.setLocalDescription(answer).then(function() {
                entry.signals.push(JSON.stringify({ type: 'answer', sdp: answer.sdp }));
            });
        }).catch(function() { entry.closed = true; });
    } else if (message.type === 'answer') {
        pc.setRemoteDescription({ type: 'answer', sdp: message.sdp }).catch(function() { entry.closed = true; });
    } else if (message.type === 'candidate') {
        pc.addIceCandidate(message.candidate).catch(function() {});
    } else {
        return 0;
    }
    return 1;
});

EM_JS(int, jbro_peer_send, (int id, int channel, const unsigned char* data, int size), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || !entry.channels[channel] || entry.channels[channel].readyState !== 'open') { return 0; }
    entry.channels[channel].send(HEAPU8.slice(data, data + size));
    return 1;
});

// 큐 맨 앞 메시지를 복사하고 크기를 돌려준다. 채널 번호는 *outChannel 에. 없으면 -1, 버퍼가 작으면 -2.
EM_JS(int, jbro_peer_take, (int id, unsigned char* out, int capacity, int* outChannel), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry || entry.queue.length === 0) { return -1; }
    var message = entry.queue[0];
    if (message.data.length > capacity) { return -2; }
    HEAPU8.set(message.data, out);
    HEAP32[outChannel >> 2] = message.channel;
    entry.queue.shift();
    return message.data.length;
});

EM_JS(void, jbro_peer_close, (int id), {
    var entry = Module.jbroNet && Module.jbroNet.items[id];
    if (!entry) { return; }
    try { if (entry.pc) { entry.pc.close(); } } catch (error) {}
    entry.closed = true;
    delete Module.jbroNet.items[id];
});

namespace JBro::Network::Web
{
    namespace
    {
        // 브라우저 WebSocket 을 바이트 흐름으로 보이게 한다. 트랜스포트가 쓴 것은 RFC6455 클라이언트 프레임(마스크 있음)이고
        // 트랜스포트가 읽는 것은 서버 프레임(마스크 없음)이다.
        class WebStreamSocket final : public IStreamSocket
        {
        public:
            WebStreamSocket()
            {
                m_outgoing.Reset(256 * 1024);
                m_incoming.Reset(256 * 1024);
            }

            ~WebStreamSocket() override
            {
                Close();
            }

            bool Connect(const char* host, std::uint16_t port) override
            {
                if (0 != m_handle)
                {
                    return false;
                }
                // https 페이지에서는 브라우저가 평문 ws 를 막는다(mixed content). 페이지가 https 면 wss 로 간다.
                const bool secure = EM_ASM_INT({ return location.protocol === 'https:' ? 1 : 0; }) != 0;
                char url[512];
                std::snprintf(url, sizeof(url), "%s://%s:%u", secure ? "wss" : "ws", host, static_cast<unsigned>(port));
                m_handle = jbro_ws_open(url);
                return 0 != m_handle;
            }

            bool Listen(std::uint16_t) override
            {
                // 브라우저는 소켓을 받을 수 없다. 웹 호스트는 피어 연결이다.
                return false;
            }

            OwnerPtr<IStreamSocket> Accept() override
            {
                return nullptr;
            }

            ConnectionState GetState() const override
            {
                if (0 == m_handle)
                {
                    return ConnectionState::Disconnected;
                }
                const int state = jbro_ws_state(m_handle);
                if (2 == state)
                {
                    return ConnectionState::Disconnected;
                }
                return 1 == state ? ConnectionState::Connected : ConnectionState::Connecting;
            }

            SocketIo Send(const void* data, std::size_t size, std::size_t& outSent) override
            {
                outSent = 0;
                if (GetState() != ConnectionState::Connected)
                {
                    return GetState() == ConnectionState::Disconnected ? SocketIo::Closed : SocketIo::WouldBlock;
                }
                if (false == m_outgoing.Write(data, static_cast<std::uint32_t>(size)))
                {
                    return SocketIo::WouldBlock;
                }
                outSent = size;
                Drain();
                return SocketIo::Ok;
            }

            SocketIo Receive(void* buffer, std::size_t capacity, std::size_t& outReceived) override
            {
                outReceived = 0;
                if (0 == m_handle)
                {
                    return SocketIo::Error;
                }
                Fill();
                const std::uint32_t count = m_incoming.Read(buffer, static_cast<std::uint32_t>(capacity));
                if (count > 0)
                {
                    outReceived = count;
                    return SocketIo::Ok;
                }
                if (GetState() == ConnectionState::Disconnected)
                {
                    return SocketIo::Closed;
                }
                return SocketIo::WouldBlock;
            }

            void Close() override
            {
                if (0 != m_handle)
                {
                    jbro_ws_close(m_handle);
                    m_handle = 0;
                }
            }

        private:
            // 트랜스포트가 쓴 바이트를 해석한다: 먼저 HTTP 업그레이드 요청(삼키고 101 을 만들어 돌려준다), 그 뒤는 프레임.
            void Drain()
            {
                std::uint8_t scratch[8192];
                while (m_outgoing.Size() > 0)
                {
                    const std::uint32_t peeked = m_outgoing.Peek(scratch, sizeof(scratch));
                    if (false == m_handshakeDone)
                    {
                        std::uint32_t consumed = 0;
                        WebSocket::ServerHandshakeRequest request;
                        const WebSocket::ParseResult result = WebSocket::ParseServerHandshake(scratch, peeked, consumed, request);
                        if (result != WebSocket::ParseResult::Ok)
                        {
                            return;
                        }
                        m_outgoing.Discard(consumed);
                        char response[512];
                        const std::uint32_t length = WebSocket::BuildServerHandshakeResponse(request, response, sizeof(response));
                        m_incoming.Write(response, length);
                        m_handshakeDone = true;
                        continue;
                    }
                    WebSocket::FrameHeader header;
                    if (WebSocket::DecodeFrameHeader(scratch, peeked, header) != WebSocket::ParseResult::Ok)
                    {
                        return;
                    }
                    const std::uint32_t total = header.headerLength + static_cast<std::uint32_t>(header.payloadLength);
                    if (m_outgoing.Size() < total)
                    {
                        return;
                    }
                    if (total > sizeof(scratch))
                    {
                        // 이 어댑터의 상한이다. 트랜스포트의 최대 메시지가 이보다 크면 여기를 키운다.
                        m_outgoing.Discard(total);
                        continue;
                    }
                    m_outgoing.Read(scratch, total);
                    std::uint8_t* payload = scratch + header.headerLength;
                    if (header.masked)
                    {
                        WebSocket::ApplyMask(payload, static_cast<std::uint32_t>(header.payloadLength), header.mask, 0);
                    }
                    if (header.opcode == WebSocket::Opcode::Binary || header.opcode == WebSocket::Opcode::Text)
                    {
                        jbro_ws_send(m_handle, payload, static_cast<int>(header.payloadLength));
                    }
                    else if (header.opcode == WebSocket::Opcode::Close)
                    {
                        Close();
                        return;
                    }
                    // Ping/Pong 은 브라우저가 스스로 처리한다. 세션 ping 은 메시지라 여기 오지 않는다.
                }
            }

            // 브라우저가 받은 메시지를 서버 프레임으로 감싸 트랜스포트가 읽게 한다.
            void Fill()
            {
                std::uint8_t body[8192];
                while (m_incoming.Free() > sizeof(body) + WebSocket::MaxFrameHeaderBytes)
                {
                    const int size = jbro_ws_take(m_handle, body, sizeof(body));
                    if (size < 0)
                    {
                        return;
                    }
                    std::uint8_t header[WebSocket::MaxFrameHeaderBytes];
                    const std::uint32_t headerLength = WebSocket::EncodeFrameHeader(
                        WebSocket::Opcode::Binary, true, static_cast<std::uint64_t>(size), false, 0, header);
                    m_incoming.Write(header, headerLength);
                    m_incoming.Write(body, static_cast<std::uint32_t>(size));
                }
            }

            int m_handle = 0;
            bool m_handshakeDone = false;
            ByteRing m_outgoing;
            ByteRing m_incoming;
        };

        class WebPeerConnection final : public IPeerConnection
        {
        public:
            explicit WebPeerConnection(const PeerConnectionDesc& desc)
            {
                // ICE 서버 목록은 널로 구분된 UTF-8 이다. JS 로는 한 글자(\u0001) 구분으로 넘긴다.
                char servers[2048] = {};
                if (nullptr != desc.iceServers)
                {
                    std::size_t at = 0;
                    const char* cursor = desc.iceServers;
                    while (*cursor != '\0' && at + 2 < sizeof(servers))
                    {
                        const std::size_t length = std::strlen(cursor);
                        if (at + length + 1 >= sizeof(servers))
                        {
                            break;
                        }
                        std::memcpy(servers + at, cursor, length);
                        at += length;
                        servers[at++] = '\x01';
                        cursor += length + 1;
                    }
                }
                m_handle = jbro_peer_open(desc.initiator ? 1 : 0, servers);
            }

            ~WebPeerConnection() override
            {
                Close();
            }

            ConnectionState GetState() const override
            {
                if (0 == m_handle)
                {
                    return ConnectionState::Disconnected;
                }
                const int state = jbro_peer_state(m_handle);
                if (2 == state)
                {
                    return ConnectionState::Disconnected;
                }
                return 1 == state ? ConnectionState::Connected : ConnectionState::Connecting;
            }

            std::uint32_t TakeSignal(void* buffer, std::uint32_t capacity) override
            {
                if (0 == m_handle)
                {
                    return 0;
                }
                const int size = jbro_peer_take_signal(m_handle, static_cast<unsigned char*>(buffer), static_cast<int>(capacity));
                return size > 0 ? static_cast<std::uint32_t>(size) : 0;
            }

            bool PushSignal(const void* data, std::uint32_t size) override
            {
                return 0 != m_handle && 0 != jbro_peer_push_signal(m_handle, static_cast<const unsigned char*>(data), static_cast<int>(size));
            }

            SocketIo Send(NetChannel channel, const void* data, std::size_t size) override
            {
                if (GetState() != ConnectionState::Connected)
                {
                    return SocketIo::WouldBlock;
                }
                return 0 != jbro_peer_send(m_handle, static_cast<int>(channel), static_cast<const unsigned char*>(data), static_cast<int>(size))
                    ? SocketIo::Ok
                    : SocketIo::WouldBlock;
            }

            SocketIo Receive(NetChannel& outChannel, void* buffer, std::size_t capacity, std::size_t& outReceived) override
            {
                outReceived = 0;
                if (0 == m_handle)
                {
                    return SocketIo::Error;
                }
                int channel = 0;
                const int size = jbro_peer_take(m_handle, static_cast<unsigned char*>(buffer), static_cast<int>(capacity), &channel);
                if (size >= 0)
                {
                    outReceived = static_cast<std::size_t>(size);
                    outChannel = static_cast<NetChannel>(channel);
                    return SocketIo::Ok;
                }
                if (-2 == size)
                {
                    return SocketIo::Error;
                }
                return GetState() == ConnectionState::Disconnected ? SocketIo::Closed : SocketIo::WouldBlock;
            }

            void Close() override
            {
                if (0 != m_handle)
                {
                    jbro_peer_close(m_handle);
                    m_handle = 0;
                }
            }

        private:
            int m_handle = 0;
        };
    }

    OwnerPtr<IStreamSocket> WebSocketProvider::CreateStreamSocket()
    {
        return MakeOwnerPtr<WebStreamSocket>();
    }

    OwnerPtr<IDatagramSocket> WebSocketProvider::CreateDatagramSocket()
    {
        // 브라우저에 UDP 는 없다. 비신뢰 채널은 WS 로 간다(트랜스포트가 알아서 한다).
        return nullptr;
    }

    OwnerPtr<IPeerConnection> WebSocketProvider::CreatePeerConnection(const PeerConnectionDesc& desc)
    {
        return MakeOwnerPtr<WebPeerConnection>(desc);
    }
}

#endif
