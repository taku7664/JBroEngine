#include "pch.h"
#include "NetworkManager.h"

#if !JBRO_PLATFORM_WEB
#include "Core/Network/Native/UdpChannel.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

// NetworkManager 는 transport(WebSocket) 위에 얇은 메시지 헤더를 얹는다.
//   와이어: [uint16 LE messageId][payload]
//   messageId 0 = raw(SetOnDataReceived 경로), 1..0xFEFF = 유저 타입드, 0xFF00~ = 시스템(세션).
namespace
{
	constexpr std::uint16_t RAW_MESSAGE_ID      = 0;
	constexpr std::size_t   MESSAGE_HEADER_SIZE = 2; // uint16 LE.

	// 세션 시스템 메시지 ID(예약 대역 0xFF00~). 유저 등록 불가.
	constexpr std::uint16_t SYSTEM_MESSAGE_BASE = 0xFF00;
	constexpr std::uint16_t SYS_HELLO           = 0xFF01;
	constexpr std::uint16_t SYS_HELLO_ACK       = 0xFF02;
	constexpr std::uint16_t SYS_BYE             = 0xFF03;
	constexpr std::uint16_t SYS_PING            = 0xFF04;
	constexpr std::uint16_t SYS_PONG            = 0xFF05;
	constexpr std::uint16_t SYS_UDP_TOKEN       = 0xFF06; // 서버→클라 UDP 연결 토큰.

	// keepalive 타이밍(ms).
	constexpr double PING_INTERVAL_MS = 1000.0; // Ready 연결에 주기 ping.
	constexpr double TIMEOUT_MS       = 5000.0; // 무응답 임계 → Timeout 종료.

	// 시스템 메시지 페이로드(POD, LE 고정).
	struct SysHelloPayload    { std::uint32_t ProtocolVersion; };
	struct SysHelloAckPayload { std::uint32_t ProtocolVersion; };
	struct SysByePayload      { std::uint8_t  Reason; };
	struct SysPingPayload     { double        SendTimeMs; };
	struct SysPongPayload     { double        EchoTimeMs; };
	struct SysUdpTokenPayload { std::uint64_t Token; };
}

// ── Construction / destruction ─────────────────────────────────────────────────

CNetworkManager::CNetworkManager(OwnerPtr<INetworkTransport> transport)
	: m_transport(std::move(transport))
{
}

CNetworkManager::~CNetworkManager()
{
	if (m_isInitialized)
	{
		Finalize();
	}
}

// ── INetworkManager ────────────────────────────────────────────────────────────

bool CNetworkManager::Initialize()
{
	if (!m_transport)
	{
		return false;
	}

	m_transport->SetOnConnected(
		[this](NetworkConnectionId id) { OnTransportConnected(id); });
	m_transport->SetOnDisconnected(
		[this](NetworkConnectionId id) { OnTransportDisconnected(id); });
	m_transport->SetOnData(
		[this](NetworkConnectionId id, const std::uint8_t* data, std::uint32_t size)
		{ OnTransportData(id, data, size); });

	m_isInitialized = true;
	return true;
}

void CNetworkManager::Finalize()
{
	// 콜백 순회 중 호출되면 미룬다(Update 말미 적용) — 순회 중 파괴 = UAF.
	if (m_deferTeardown)
	{
		m_pendingFinalize = true;
		return;
	}

	if (m_transport)
	{
		m_transport->Close();
	}
#if !JBRO_PLATFORM_WEB
	if (m_udp) { m_udp->Stop(); m_udp.Reset(); }
#endif
	m_connections.clear();
	m_typeIds.clear();
	m_messageHandlers.clear();
	m_role           = ENetworkRole::None;
	m_isInitialized  = false;
}

bool CNetworkManager::Connect(const char* host, std::uint16_t port)
{
	if (!m_transport)
	{
		return false;
	}
	m_role = ENetworkRole::Client;
	// 서버 주소 기억 — 토큰 수신 후 UDP 클라 소켓이 서버 UDP 엔드포인트를 만들 때 재사용.
	// UDP resolve 는 순수 호스트만 받으므로 ws://,wss:// 스킴을 벗겨 보관(스킴 처리는 transport 담당).
	m_serverHost = (nullptr != host) ? host : "";
	if (0 == m_serverHost.rfind("wss://", 0))      { m_serverHost = m_serverHost.substr(6); }
	else if (0 == m_serverHost.rfind("ws://", 0))  { m_serverHost = m_serverHost.substr(5); }
	m_serverPort = port;
	return m_transport->Connect(host, port); // 원본(스킴 포함) 전달 — transport 가 스킴 파싱.
}

bool CNetworkManager::StartServer(std::uint16_t port)
{
	if (!m_transport)
	{
		return false;
	}
	m_role = ENetworkRole::Server;

#if !JBRO_PLATFORM_WEB
	// TCP 와 동일 포트에 UDP 바인드(비신뢰 채널). 실패해도 신뢰 채널은 정상 — 치명 아님.
	m_udp = MakeOwnerPtr<CUdpChannel>();
	if (false == m_udp->StartServer(port))
	{
		m_udp.Reset();
	}
#endif

	return m_transport->Listen(port);
}

void CNetworkManager::Disconnect()
{
	if (m_deferTeardown)
	{
		m_pendingDisconnect = true;
		return;
	}

	if (m_transport)
	{
		m_transport->Close();
	}
#if !JBRO_PLATFORM_WEB
	if (m_udp) { m_udp->Stop(); m_udp.Reset(); }
#endif
	m_connections.clear();
	m_sessions.clear();
	m_role = ENetworkRole::None;
}

void CNetworkManager::DisconnectClient(NetworkConnectionId id)
{
	if (m_deferTeardown)
	{
		m_pendingClientCloses.push_back(id);
		return;
	}
	if (m_transport)
	{
		m_transport->CloseConnection(id);
	}
}

bool CNetworkManager::IsConnected() const
{
	if (!m_transport || m_connections.empty())
	{
		return false;
	}
	return ENetworkConnectionState::Connected == m_transport->GetConnectionState(m_connections[0]);
}

bool CNetworkManager::IsListening() const
{
	return m_transport && m_transport->IsListening();
}

ENetworkRole CNetworkManager::GetRole() const
{
	return m_role;
}

double CNetworkManager::GetRoundTripMs(NetworkConnectionId id) const
{
	const auto it = m_sessions.find(id);
	return (m_sessions.end() != it) ? it->second.RoundTripMs : -1.0;
}

double CNetworkManager::GetUdpLossRate(NetworkConnectionId id) const
{
#if !JBRO_PLATFORM_WEB
	if (m_udp)
	{
		return m_udp->GetLossRate(id);
	}
#else
	(void)id;
#endif
	return -1.0; // 웹/UDP 미활성 — 비신뢰는 신뢰 폴백이라 유실 없음(측정 불필요).
}

void CNetworkManager::SetSecureServerCertificate(void* certContext)
{
	if (m_transport)
	{
		m_transport->SetSecureServer(certContext);
	}
}

void CNetworkManager::SetSecureClientOptions(const char* hostName, bool skipCertValidation)
{
	if (m_transport)
	{
		m_transport->SetSecureClient(hostName, skipCertValidation);
	}
}

bool CNetworkManager::Send(NetworkConnectionId id, const void* data, std::uint32_t size)
{
	return SendFramed(id, RAW_MESSAGE_ID, data, size, ENetChannel::ReliableOrdered);
}

bool CNetworkManager::Broadcast(const void* data, std::uint32_t size)
{
	return BroadcastMessageBytes(RAW_MESSAGE_ID, data, size, ENetChannel::ReliableOrdered);
}

void CNetworkManager::Update()
{
	// 콜백(핸들러) 순회 구간 — 이 안에서 요청된 teardown 은 아래에서 지연 적용.
	m_deferTeardown = true;

	if (m_transport)
	{
		m_transport->Update(); // 콜백 발화(수신/연결/종료). 여기서 WantsClose 가 설정될 수 있음.
	}

#if !JBRO_PLATFORM_WEB
	if (m_udp)
	{
		m_udp->Poll([this](NetworkConnectionId id, std::uint16_t msgId,
			const std::uint8_t* payload, std::uint32_t size)
		{
			DispatchUserMessage(id, msgId, payload, size);
		});
	}
#endif

	m_deferTeardown = false;

	// 콜백 순회가 끝났으니 지연된 teardown 을 안전하게 적용.
	if (m_pendingFinalize)
	{
		m_pendingFinalize   = false;
		m_pendingDisconnect = false;
		m_pendingClientCloses.clear();
		Finalize();
		return;
	}
	if (m_pendingDisconnect)
	{
		m_pendingDisconnect = false;
		m_pendingClientCloses.clear();
		Disconnect();
		return;
	}
	if (false == m_pendingClientCloses.empty())
	{
		std::vector<NetworkConnectionId> closes;
		closes.swap(m_pendingClientCloses);
		for (NetworkConnectionId id : closes)
		{
			DisconnectClient(id);
		}
	}

	UpdateSessions(); // ping 송신 / 타임아웃 감지 → WantsClose 설정.

	// 지연 종료 처리. 콜백(onData) 재진입 중 transport 를 건드리면 UAF 위험이라
	// 실제 CloseConnection 은 여기(Update 말미)서만 한다. id 먼저 스냅샷.
	std::vector<NetworkConnectionId> toClose;
	for (const auto& entry : m_sessions)
	{
		if (entry.second.WantsClose)
		{
			toClose.push_back(entry.first);
		}
	}
	for (NetworkConnectionId id : toClose)
	{
		if (m_transport)
		{
			m_transport->CloseConnection(id); // → OnTransportDisconnected 에서 세션 정리 + 콜백.
		}
	}
}

void CNetworkManager::SetOnConnected(FOnNetworkConnected callback)
{
	m_onConnected = std::move(callback);
}

void CNetworkManager::SetOnDisconnected(FOnNetworkDisconnected callback)
{
	m_onDisconnected = std::move(callback);
}

void CNetworkManager::SetOnDataReceived(FOnNetworkDataReceived callback)
{
	m_onDataReceived = std::move(callback);
}

// ── Transport callbacks ────────────────────────────────────────────────────────

void CNetworkManager::OnTransportConnected(NetworkConnectionId id)
{
	// 전송 연결됨 = 세션 시작(아직 게임엔 통보 안 함). hello 합의 후 PromoteToReady 에서 통보.
	ConnectionSession session;
	session.State          = ESessionState::Handshaking;
	session.LastRecvMs     = NowMs();
	session.LastPingSentMs = NowMs();
	m_sessions[id]         = session;

	// 클라이언트가 hello 를 먼저 보낸다(서버는 수신 후 검증·응답).
	if (ENetworkRole::Client == m_role)
	{
		SysHelloPayload hello{ NETWORK_PROTOCOL_VERSION };
		SendSystem(id, SYS_HELLO, hello);
	}
}

void CNetworkManager::OnTransportDisconnected(NetworkConnectionId id)
{
	const auto it = m_sessions.find(id);
	const ENetworkDisconnectReason reason =
		(m_sessions.end() != it) ? it->second.PendingReason : ENetworkDisconnectReason::Normal;
	const bool wasReady =
		(m_sessions.end() != it) && (ESessionState::Ready == it->second.State);

	m_sessions.erase(id);
	m_connections.erase(
		std::remove(m_connections.begin(), m_connections.end(), id),
		m_connections.end());

#if !JBRO_PLATFORM_WEB
	if (m_udp) { m_udp->RemovePeer(id); }
#endif

	// 통보 대상:
	//  - Ready 였던 연결(게임이 OnConnected 를 이미 받음) → 종료 통보.
	//  - 클라이언트가 시도한 연결 → 핸드셰이크 실패(버전 불일치 등)도 결과를 알려야 함.
	//  - 서버가 수락한 미완 연결(스캐너/버전불일치 랜덤) → 게임에 노출 안 함.
	const bool notify = wasReady || (ENetworkRole::Client == m_role);
	if (notify && m_onDisconnected)
	{
		m_onDisconnected(id, reason);
	}
}

void CNetworkManager::OnTransportData(
	NetworkConnectionId id, const std::uint8_t* data, std::uint32_t size)
{
	if (size < MESSAGE_HEADER_SIZE)
	{
		return; // 헤더 없는 프레임 — 우리 송신 규약상 발생하지 않음. 방어적으로 무시.
	}

	const std::uint16_t messageId =
		static_cast<std::uint16_t>(data[0]) |
		(static_cast<std::uint16_t>(data[1]) << 8);

	const std::uint8_t* payload     = data + MESSAGE_HEADER_SIZE;
	const std::uint32_t payloadSize = size - static_cast<std::uint32_t>(MESSAGE_HEADER_SIZE);

	// 어떤 프레임이든 수신은 liveness 신호.
	const auto sessionIt = m_sessions.find(id);
	if (m_sessions.end() != sessionIt)
	{
		sessionIt->second.LastRecvMs = NowMs();
	}

	// 시스템(세션) 메시지는 게임에 노출하지 않고 내부 처리.
	if (messageId >= SYSTEM_MESSAGE_BASE)
	{
		HandleSystemMessage(id, messageId, payload, payloadSize);
		return;
	}

	DispatchUserMessage(id, messageId, payload, payloadSize);
}

void CNetworkManager::DispatchUserMessage(
	NetworkConnectionId id, std::uint16_t messageId, const std::uint8_t* payload, std::uint32_t size)
{
	if (messageId >= SYSTEM_MESSAGE_BASE)
	{
		return; // 시스템 메시지는 유저 경로로 오지 않는다(UDP 방어).
	}

	if (RAW_MESSAGE_ID == messageId)
	{
		if (m_onDataReceived)
		{
			m_onDataReceived(id, payload, size);
		}
		return;
	}

	const auto it = m_messageHandlers.find(messageId);
	if (m_messageHandlers.end() != it && it->second)
	{
		it->second(id, payload, size);
	}
}

// ── 타입드 메시지 ────────────────────────────────────────────────────────────────

void CNetworkManager::RegisterMessageType(const void* typeTag, std::uint16_t messageId)
{
	// 0(raw) 과 시스템 예약 대역(0xFF00~)은 유저 등록 불가. 유효 범위 1..0xFEFF.
	if (RAW_MESSAGE_ID == messageId || messageId >= SYSTEM_MESSAGE_BASE)
	{
		return;
	}
	m_typeIds[typeTag] = messageId;
}

std::uint16_t CNetworkManager::MessageIdForType(const void* typeTag) const
{
	const auto it = m_typeIds.find(typeTag);
	return (m_typeIds.end() != it) ? it->second : RAW_MESSAGE_ID;
}

void CNetworkManager::RegisterMessageHandler(std::uint16_t messageId, FNetworkMessageHandler handler)
{
	m_messageHandlers[messageId] = std::move(handler);
}

bool CNetworkManager::SendMessageBytes(
	NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel)
{
	return SendFramed(id, messageId, data, size, channel);
}

bool CNetworkManager::BroadcastMessageBytes(
	std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel)
{
	if (!m_transport || m_connections.empty())
	{
		return false;
	}

	bool allOk = true;
	for (NetworkConnectionId id : m_connections)
	{
		if (false == SendFramed(id, messageId, data, size, channel))
		{
			allOk = false;
		}
	}
	return allOk;
}

bool CNetworkManager::SendFramed(
	NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel)
{
	if (!m_transport)
	{
		return false;
	}

#if !JBRO_PLATFORM_WEB
	// 비신뢰 채널은 UDP 로 시도. 미준비(엔드포인트/토큰 없음)·대용량이면 false → 신뢰 폴백.
	if (ENetChannel::ReliableOrdered != channel && m_udp &&
	    m_udp->Send(id, channel, messageId, data, size))
	{
		return true;
	}
#else
	(void)channel; // 웹은 UDP 불가 — 전 채널 신뢰 WS.
#endif

	// 신뢰 경로: WS transport 로 [uint16 msgId][payload] 프레임 전송.
	std::vector<std::uint8_t> frame;
	frame.reserve(MESSAGE_HEADER_SIZE + size);
	frame.push_back(static_cast<std::uint8_t>(messageId & 0xFFu));
	frame.push_back(static_cast<std::uint8_t>((messageId >> 8) & 0xFFu));
	if (size > 0 && nullptr != data)
	{
		const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
		frame.insert(frame.end(), bytes, bytes + size);
	}

	return m_transport->Send(id, frame.data(), static_cast<std::uint32_t>(frame.size()));
}

// ── 세션 계층 구현 ────────────────────────────────────────────────────────────

double CNetworkManager::NowMs() const
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration<double, std::milli>(now).count();
}

template<typename T>
void CNetworkManager::SendSystem(NetworkConnectionId id, std::uint16_t messageId, const T& message)
{
	// 세션 제어 메시지는 항상 신뢰(순서보장).
	SendFramed(id, messageId, &message, static_cast<std::uint32_t>(sizeof(T)), ENetChannel::ReliableOrdered);
}

void CNetworkManager::PromoteToReady(NetworkConnectionId id)
{
	const auto it = m_sessions.find(id);
	if (m_sessions.end() == it || ESessionState::Ready == it->second.State)
	{
		return;
	}

	it->second.State          = ESessionState::Ready;
	it->second.LastRecvMs     = NowMs();
	it->second.LastPingSentMs = NowMs();

	if (m_connections.end() == std::find(m_connections.begin(), m_connections.end(), id))
	{
		m_connections.push_back(id);
	}

#if !JBRO_PLATFORM_WEB
	// 서버: 이 연결의 UDP 토큰을 발급하고 신뢰 채널로 클라에 전달(클라가 UDP 를 켠다).
	if (ENetworkRole::Server == m_role && m_udp)
	{
		const std::uint64_t token = m_udp->RegisterServerPeer(id);
		SysUdpTokenPayload payload{ token };
		SendSystem(id, SYS_UDP_TOKEN, payload);
	}
#endif

	if (m_onConnected)
	{
		m_onConnected(id);
	}
}

void CNetworkManager::DropConnection(NetworkConnectionId id, ENetworkDisconnectReason reason)
{
	// 즉시 닫지 않고 플래그만. 실제 CloseConnection 은 Update 말미(재진입 안전).
	const auto it = m_sessions.find(id);
	if (m_sessions.end() == it)
	{
		return;
	}
	it->second.PendingReason = reason;
	it->second.WantsClose    = true;
}

bool CNetworkManager::HandleSystemMessage(
	NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size)
{
	switch (messageId)
	{
	case SYS_HELLO:
	{
		// 서버가 클라 hello 를 검증.
		if (sizeof(SysHelloPayload) != size)
		{
			DropConnection(id, ENetworkDisconnectReason::Error);
			return true;
		}
		SysHelloPayload hello{};
		std::memcpy(&hello, data, sizeof(hello));

		if (NETWORK_PROTOCOL_VERSION != hello.ProtocolVersion)
		{
			// 거부 사유(Bye)를 보낸다. 단, **닫지 않는다** — pending 데이터가 있는 쪽이
			// 먼저 closesocket 하면 RST 가 버퍼된 Bye 를 삼킨다(수신측 데이터 유실).
			// 클라가 Bye 를 읽고 스스로 닫거나(clean FIN), 핸드셰이크 타임아웃(5s)이 정리한다.
			SysByePayload bye{ static_cast<std::uint8_t>(ENetworkDisconnectReason::VersionMismatch) };
			SendSystem(id, SYS_BYE, bye);
			return true;
		}

		SysHelloAckPayload ack{ NETWORK_PROTOCOL_VERSION };
		SendSystem(id, SYS_HELLO_ACK, ack);
		PromoteToReady(id);
		return true;
	}
	case SYS_HELLO_ACK:
	{
		// 클라가 서버 수락을 확인 → 세션 준비 완료.
		PromoteToReady(id);
		return true;
	}
	case SYS_BYE:
	{
		ENetworkDisconnectReason reason = ENetworkDisconnectReason::Normal;
		if (sizeof(SysByePayload) == size)
		{
			SysByePayload bye{};
			std::memcpy(&bye, data, sizeof(bye));
			reason = static_cast<ENetworkDisconnectReason>(bye.Reason);
		}
		DropConnection(id, reason);
		return true;
	}
	case SYS_PING:
	{
		if (sizeof(SysPingPayload) == size)
		{
			SysPingPayload ping{};
			std::memcpy(&ping, data, sizeof(ping));
			SysPongPayload pong{ ping.SendTimeMs }; // 상대 시각 그대로 에코.
			SendSystem(id, SYS_PONG, pong);
		}
		return true;
	}
	case SYS_PONG:
	{
		if (sizeof(SysPongPayload) == size)
		{
			SysPongPayload pong{};
			std::memcpy(&pong, data, sizeof(pong));
			const auto it = m_sessions.find(id);
			if (m_sessions.end() != it)
			{
				it->second.RoundTripMs = NowMs() - pong.EchoTimeMs;
			}
		}
		return true;
	}
	case SYS_UDP_TOKEN:
	{
		// 클라: 서버가 준 UDP 토큰. UDP 소켓 열고 토큰 등록 → 비신뢰 채널 활성.
		// 웹은 UDP 불가라 무시(비신뢰는 신뢰 WS 로 유지).
#if !JBRO_PLATFORM_WEB
		if (ENetworkRole::Client == m_role && sizeof(SysUdpTokenPayload) == size)
		{
			SysUdpTokenPayload payload{};
			std::memcpy(&payload, data, sizeof(payload));

			if (!m_udp)
			{
				m_udp = MakeOwnerPtr<CUdpChannel>();
				if (false == m_udp->StartClient(m_serverHost.c_str(), m_serverPort))
				{
					m_udp.Reset(); // UDP 못 열면 비신뢰는 신뢰로 폴백.
				}
			}
			if (m_udp)
			{
				m_udp->SetClientToken(payload.Token);
			}
		}
#endif
		return true;
	}
	default:
		return true; // 미지의 시스템 메시지는 무시(전방호환).
	}
}

void CNetworkManager::UpdateSessions()
{
	const double now = NowMs();

	for (auto& entry : m_sessions)
	{
		ConnectionSession& session = entry.second;
		if (session.WantsClose)
		{
			continue; // 이미 종료 예정.
		}

		// 무응답 타임아웃(핸드셰이크·Ready 공통) → Timeout 종료.
		if (now - session.LastRecvMs > TIMEOUT_MS)
		{
			session.PendingReason = ENetworkDisconnectReason::Timeout;
			session.WantsClose    = true;
			continue;
		}

		// Ready 연결은 주기적으로 ping 을 보내 liveness/RTT 유지.
		if (ESessionState::Ready == session.State &&
		    now - session.LastPingSentMs >= PING_INTERVAL_MS)
		{
			session.LastPingSentMs = now;
			SysPingPayload ping{ now };
			SendSystem(entry.first, SYS_PING, ping);

#if !JBRO_PLATFORM_WEB
			// 클라: UDP punch 를 함께 보내 NAT 유지 + 서버의 엔드포인트 최신화.
			if (ENetworkRole::Client == m_role && m_udp)
			{
				m_udp->SendPunch(entry.first);
			}
#endif
		}
	}
}
