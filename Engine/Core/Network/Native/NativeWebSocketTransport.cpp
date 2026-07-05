#include "pch.h"
#include "NativeWebSocketTransport.h"

#if !JBRO_PLATFORM_WEB

#include "Core/Network/WebSocket/HandshakeCrypto.h"

#include <string>

namespace
{
	constexpr std::size_t MAX_MESSAGE_SIZE = 8u * 1024u * 1024u; // 8MB 조립 상한.
	constexpr std::size_t RECV_CHUNK       = 8192u;
}

CNativeWebSocketTransport::CNativeWebSocketTransport() = default;

CNativeWebSocketTransport::~CNativeWebSocketTransport()
{
	Close();
}

// ── INetworkTransport ──────────────────────────────────────────────────────────

bool CNativeWebSocketTransport::Connect(const char* host, std::uint16_t port)
{
	if (nullptr == host)
	{
		return false;
	}

	OwnerPtr<ISocket> socket = CreateTcpSocket();
	if (!socket || false == socket->Connect(host, port))
	{
		return false;
	}

	Connection conn;
	conn.Socket       = std::move(socket);
	conn.Phase        = EPhase::TcpConnecting;
	conn.IsServerSide = false;
	conn.HostHeader   = std::string(host) + ":" + std::to_string(static_cast<unsigned>(port));

	const NetworkConnectionId id = AllocConnectionId();
	m_connections.emplace(id, std::move(conn));
	return true;
}

bool CNativeWebSocketTransport::Listen(std::uint16_t port)
{
	OwnerPtr<ISocket> socket = CreateTcpSocket();
	if (!socket || false == socket->Listen(port))
	{
		return false;
	}
	m_listenSocket = std::move(socket);
	m_isListening  = true;
	return true;
}

void CNativeWebSocketTransport::Close()
{
	std::vector<NetworkConnectionId> ids;
	ids.reserve(m_connections.size());
	for (const auto& entry : m_connections)
	{
		ids.push_back(entry.first);
	}
	for (NetworkConnectionId id : ids)
	{
		RemoveConnection(id);
	}

	if (m_listenSocket)
	{
		m_listenSocket->Close();
		m_listenSocket.Reset();
	}
	m_isListening = false;
}

void CNativeWebSocketTransport::CloseConnection(NetworkConnectionId id)
{
	RemoveConnection(id);
}

bool CNativeWebSocketTransport::IsListening() const
{
	return m_isListening;
}

ENetworkConnectionState CNativeWebSocketTransport::GetConnectionState(NetworkConnectionId id) const
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it)
	{
		return ENetworkConnectionState::Disconnected;
	}
	return (EPhase::Open == it->second.Phase)
		? ENetworkConnectionState::Connected
		: ENetworkConnectionState::Connecting;
}

bool CNativeWebSocketTransport::Send(NetworkConnectionId id, const void* data, std::uint32_t size)
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it || EPhase::Open != it->second.Phase)
	{
		return false;
	}

	QueueFrame(it->second, WebSocket::EOpcode::Binary, data, size);
	FlushSendBuffer(it->second);
	return true;
}

void CNativeWebSocketTransport::Update()
{
	if (m_isListening)
	{
		AcceptPending();
	}

	// PollConnection 이 m_connections 를 지울 수 있어 id 를 먼저 스냅샷.
	std::vector<NetworkConnectionId> ids;
	ids.reserve(m_connections.size());
	for (const auto& entry : m_connections)
	{
		ids.push_back(entry.first);
	}

	for (NetworkConnectionId id : ids)
	{
		auto it = m_connections.find(id);
		if (m_connections.end() != it)
		{
			ServiceConnection(id, it->second);
		}
	}
}

void CNativeWebSocketTransport::SetOnConnected(FOnTransportConnected callback)
{
	m_onConnected = std::move(callback);
}

void CNativeWebSocketTransport::SetOnDisconnected(FOnTransportDisconnected callback)
{
	m_onDisconnected = std::move(callback);
}

void CNativeWebSocketTransport::SetOnData(FOnTransportData callback)
{
	m_onData = std::move(callback);
}

// ── 내부 구현 ────────────────────────────────────────────────────────────────────

void CNativeWebSocketTransport::AcceptPending()
{
	if (!m_listenSocket)
	{
		return;
	}
	for (;;)
	{
		OwnerPtr<ISocket> client = m_listenSocket->Accept();
		if (!client)
		{
			break; // 대기 연결 없음.
		}

		Connection conn;
		conn.Socket       = std::move(client);
		conn.Phase        = EPhase::Handshaking;
		conn.IsServerSide = true;

		const NetworkConnectionId id = AllocConnectionId();
		m_connections.emplace(id, std::move(conn));
	}
}

void CNativeWebSocketTransport::ServiceConnection(NetworkConnectionId id, Connection& conn)
{
	// 1) 대기 송신 우선 드레인.
	FlushSendBuffer(conn);

	// 2) 클라 논블로킹 connect 진행.
	if (EPhase::TcpConnecting == conn.Phase)
	{
		const ESocketConnect result = conn.Socket ? conn.Socket->PollConnect() : ESocketConnect::Failed;
		if (ESocketConnect::Pending == result)
		{
			return;
		}
		if (ESocketConnect::Failed == result)
		{
			RemoveConnection(id);
			return;
		}

		// 연결됨 → 클라 핸드셰이크 요청 전송(원시 HTTP).
		conn.ClientKey = WebSocket::GenerateClientKey(
			(static_cast<std::uint64_t>(id) << 32) ^ NextMaskKey());
		const std::string request =
			WebSocket::BuildClientHandshakeRequest(conn.HostHeader, "/", conn.ClientKey);
		conn.SendBuffer.insert(conn.SendBuffer.end(), request.begin(), request.end());
		conn.HandshakeSent = true;
		conn.Phase         = EPhase::Handshaking;
		FlushSendBuffer(conn);
	}

	// 3) 소켓에서 읽기.
	if (false == ReadIntoRecvBuffer(id, conn))
	{
		return; // 드롭됨.
	}

	// 4) 단계별 처리.
	if (EPhase::Handshaking == conn.Phase)
	{
		if (false == PumpHandshake(id, conn))
		{
			return;
		}
	}
	if (EPhase::Open == conn.Phase)
	{
		if (false == PumpFrames(id, conn))
		{
			return;
		}
	}

	// 5) 처리 중 큐된 응답/Pong 송신.
	FlushSendBuffer(conn);
}

bool CNativeWebSocketTransport::ReadIntoRecvBuffer(NetworkConnectionId id, Connection& conn)
{
	if (!conn.Socket)
	{
		RemoveConnection(id);
		return false;
	}

	std::uint8_t chunk[RECV_CHUNK];
	for (;;)
	{
		std::size_t got = 0;
		const ESocketIo result = conn.Socket->Recv(chunk, sizeof(chunk), got);
		if (ESocketIo::Ok == result)
		{
			conn.RecvBuffer.insert(conn.RecvBuffer.end(), chunk, chunk + got);
			if (conn.RecvBuffer.size() > MAX_MESSAGE_SIZE * 2u)
			{
				RemoveConnection(id); // 폭주 방어.
				return false;
			}
			continue;
		}
		if (ESocketIo::WouldBlock == result)
		{
			break;
		}
		// Closed 또는 Error.
		RemoveConnection(id);
		return false;
	}
	return true;
}

bool CNativeWebSocketTransport::PumpHandshake(NetworkConnectionId id, Connection& conn)
{
	std::size_t consumed = 0;

	if (conn.IsServerSide)
	{
		WebSocket::ServerHandshakeRequest request;
		const WebSocket::EParse parse = WebSocket::ParseServerHandshake(
			conn.RecvBuffer.data(), conn.RecvBuffer.size(), consumed, request);

		if (WebSocket::EParse::NeedMoreData == parse)
		{
			return true;
		}
		if (WebSocket::EParse::Invalid == parse)
		{
			RemoveConnection(id);
			return false;
		}

		conn.RecvBuffer.erase(conn.RecvBuffer.begin(),
			conn.RecvBuffer.begin() + static_cast<std::ptrdiff_t>(consumed));

		const std::string response = WebSocket::BuildServerHandshakeResponse(request);
		conn.SendBuffer.insert(conn.SendBuffer.end(), response.begin(), response.end());
		conn.Phase = EPhase::Open;
	}
	else
	{
		const std::string expectedAccept = WebSocket::ComputeAcceptKey(conn.ClientKey);
		const WebSocket::EParse parse = WebSocket::ParseClientHandshakeResponse(
			conn.RecvBuffer.data(), conn.RecvBuffer.size(), consumed, expectedAccept);

		if (WebSocket::EParse::NeedMoreData == parse)
		{
			return true;
		}
		if (WebSocket::EParse::Invalid == parse)
		{
			RemoveConnection(id);
			return false;
		}

		conn.RecvBuffer.erase(conn.RecvBuffer.begin(),
			conn.RecvBuffer.begin() + static_cast<std::ptrdiff_t>(consumed));
		conn.Phase = EPhase::Open;
	}

	// 핸드셰이크 완료 → 연결 보고.
	conn.Reported = true;
	if (m_onConnected)
	{
		m_onConnected(id);
	}
	return true;
}

bool CNativeWebSocketTransport::PumpFrames(NetworkConnectionId id, Connection& conn)
{
	for (;;)
	{
		std::size_t consumed = 0;
		WebSocket::DecodedFrame frame;
		const WebSocket::EParse parse = WebSocket::DecodeFrame(
			conn.RecvBuffer.data(), conn.RecvBuffer.size(), consumed, frame);

		if (WebSocket::EParse::NeedMoreData == parse)
		{
			break;
		}
		if (WebSocket::EParse::Invalid == parse)
		{
			RemoveConnection(id);
			return false;
		}

		conn.RecvBuffer.erase(conn.RecvBuffer.begin(),
			conn.RecvBuffer.begin() + static_cast<std::ptrdiff_t>(consumed));

		switch (frame.Opcode)
		{
		case WebSocket::EOpcode::Close:
		{
			// Close 에코 후 종료.
			QueueFrame(conn, WebSocket::EOpcode::Close, nullptr, 0);
			FlushSendBuffer(conn);
			RemoveConnection(id);
			return false;
		}
		case WebSocket::EOpcode::Ping:
		{
			QueueFrame(conn, WebSocket::EOpcode::Pong,
				frame.Payload.data(), frame.Payload.size());
			break;
		}
		case WebSocket::EOpcode::Pong:
		{
			break; // 무시.
		}
		case WebSocket::EOpcode::Binary:
		case WebSocket::EOpcode::Text:
		case WebSocket::EOpcode::Continuation:
		{
			const bool isStart = (WebSocket::EOpcode::Continuation != frame.Opcode);
			if (isStart)
			{
				if (conn.InFragment)
				{
					RemoveConnection(id); // 이전 조각 미완인데 새 데이터 시작 — 위반.
					return false;
				}
				conn.FragmentData   = std::move(frame.Payload);
				conn.FragmentOpcode = frame.Opcode;
				conn.InFragment     = true;
			}
			else
			{
				if (false == conn.InFragment)
				{
					RemoveConnection(id); // 시작 없는 continuation — 위반.
					return false;
				}
				conn.FragmentData.insert(conn.FragmentData.end(),
					frame.Payload.begin(), frame.Payload.end());
			}

			if (conn.FragmentData.size() > MAX_MESSAGE_SIZE)
			{
				RemoveConnection(id);
				return false;
			}

			if (frame.Fin)
			{
				if (m_onData)
				{
					m_onData(id, conn.FragmentData.data(),
						static_cast<std::uint32_t>(conn.FragmentData.size()));
				}
				conn.FragmentData.clear();
				conn.InFragment = false;
			}
			break;
		}
		default:
		{
			RemoveConnection(id); // 미지원 opcode.
			return false;
		}
		}
	}
	return true;
}

void CNativeWebSocketTransport::FlushSendBuffer(Connection& conn)
{
	if (!conn.Socket || conn.SendBuffer.empty())
	{
		return;
	}

	std::size_t offset = 0;
	while (offset < conn.SendBuffer.size())
	{
		std::size_t sent = 0;
		const ESocketIo result = conn.Socket->Send(
			conn.SendBuffer.data() + offset, conn.SendBuffer.size() - offset, sent);

		if (ESocketIo::Ok == result)
		{
			offset += sent;
			continue;
		}
		if (ESocketIo::WouldBlock == result)
		{
			break; // 송신 버퍼 가득 — 다음 Update 에서 재시도.
		}
		// Error — 소켓 종료. 다음 read 사이클이 연결을 제거하며 disconnect 보고.
		conn.Socket->Close();
		break;
	}

	if (offset > 0)
	{
		conn.SendBuffer.erase(conn.SendBuffer.begin(),
			conn.SendBuffer.begin() + static_cast<std::ptrdiff_t>(offset));
	}
}

void CNativeWebSocketTransport::QueueFrame(
	Connection& conn, WebSocket::EOpcode opcode, const void* data, std::size_t size)
{
	// 클라이언트→서버 프레임은 마스크, 서버→클라이언트는 언마스크(RFC6455).
	const bool          mask    = (false == conn.IsServerSide);
	const std::uint32_t maskKey = mask ? NextMaskKey() : 0u;
	WebSocket::EncodeFrame(conn.SendBuffer, opcode, data, size, mask, maskKey);
}

void CNativeWebSocketTransport::RemoveConnection(NetworkConnectionId id)
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it)
	{
		return;
	}

	if (it->second.Socket)
	{
		it->second.Socket->Close();
	}

	const bool wasReported = it->second.Reported;
	m_connections.erase(it);

	if (wasReported && m_onDisconnected)
	{
		m_onDisconnected(id);
	}
}

NetworkConnectionId CNativeWebSocketTransport::AllocConnectionId()
{
	return m_nextId++;
}

std::uint32_t CNativeWebSocketTransport::NextMaskKey()
{
	// xorshift32 — 프레임 마스크용(보안 아님).
	std::uint32_t x = m_maskRng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	m_maskRng = x;
	return x;
}

#endif // !JBRO_PLATFORM_WEB
