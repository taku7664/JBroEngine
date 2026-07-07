#include "pch.h"
#include "UdpChannel.h"
#include "UdpDatagram.h" // v2 코덱(UdpProto::Encode/Decode)

#if !JBRO_PLATFORM_WEB

#include <chrono>
#include <cstring>
#include <vector>

namespace
{
	constexpr std::uint32_t UDP_MAX_PAYLOAD  = 1024u; // MTU 여유(IP 분할 회피).
	constexpr std::size_t   UDP_RECV_BUFSIZE = 2048u;
}

// ── 수명 ─────────────────────────────────────────────────────────────────────────

bool CUdpChannel::StartServer(std::uint16_t port)
{
	m_socket = CreateUdpSocket();
	if (m_socket && m_socketDecorator)
	{
		m_socket = m_socketDecorator(std::move(m_socket)); // 테스트: 유실 데코레이터로 감쌈.
	}
	if (!m_socket || false == m_socket->Bind(port))
	{
		m_socket.Reset();
		return false;
	}
	m_role = ENetworkRole::Server;
	return true;
}

bool CUdpChannel::StartClient(const char* serverHost, std::uint16_t serverPort)
{
	m_socket = CreateUdpSocket();
	if (m_socket && m_socketDecorator)
	{
		m_socket = m_socketDecorator(std::move(m_socket)); // 테스트: 유실 데코레이터로 감쌈.
	}
	if (!m_socket || false == m_socket->Open() ||
	    false == m_socket->Resolve(serverHost, serverPort, m_serverEndpoint))
	{
		m_socket.Reset();
		return false;
	}
	m_role = ENetworkRole::Client;
	return true;
}

void CUdpChannel::Stop()
{
	if (m_socket)
	{
		m_socket->Close();
		m_socket.Reset();
	}
	m_peers.clear();
	m_tokenToPeer.clear();
	m_clientTokenSet = false;
	m_clientToken    = 0;
	m_serverEndpoint = NetUdpEndpoint{};
	m_clientRecvLastSeq.clear();
	m_clientStats = RecvStats{};
	m_clientReliable.Reset();
	m_role = ENetworkRole::None;
}

// ── 연결 매핑 ────────────────────────────────────────────────────────────────────

std::uint64_t CUdpChannel::RegisterServerPeer(NetworkConnectionId id)
{
	std::uint64_t token = NextToken();
	while (m_tokenToPeer.find(token) != m_tokenToPeer.end())
	{
		token = NextToken();
	}

	ServerPeer peer;
	peer.Token = token;
	m_peers[id] = std::move(peer);
	m_tokenToPeer[token] = id;
	return token;
}

void CUdpChannel::RemovePeer(NetworkConnectionId id)
{
	const auto it = m_peers.find(id);
	if (m_peers.end() != it)
	{
		m_tokenToPeer.erase(it->second.Token);
		m_peers.erase(it);
	}
}

double CUdpChannel::GetLossRate(NetworkConnectionId id) const
{
	if (ENetworkRole::Server == m_role)
	{
		const auto it = m_peers.find(id);
		return (m_peers.end() != it) ? it->second.Stats.LossRate() : -1.0;
	}
	if (ENetworkRole::Client == m_role)
	{
		return m_clientStats.LossRate();
	}
	return -1.0;
}

void CUdpChannel::SetClientToken(std::uint64_t token)
{
	m_clientToken    = token;
	m_clientTokenSet = true;
	SendPunch(SERVER_CONNECTION_ID); // 서버가 우리 엔드포인트를 학습하도록 즉시 노크.
}

// ── 송신 ─────────────────────────────────────────────────────────────────────────

bool CUdpChannel::CanSend(NetworkConnectionId id, std::uint32_t payloadSize) const
{
	if (!m_socket || payloadSize > UDP_MAX_PAYLOAD)
	{
		return false;
	}
	if (ENetworkRole::Server == m_role)
	{
		const auto it = m_peers.find(id);
		return m_peers.end() != it && it->second.Endpoint.Valid();
	}
	if (ENetworkRole::Client == m_role)
	{
		return m_clientTokenSet && m_serverEndpoint.Valid();
	}
	return false;
}

bool CUdpChannel::Send(
	NetworkConnectionId id, ENetChannel channel, std::uint16_t msgId, const void* data, std::uint32_t size)
{
	if (false == CanSend(id, size))
	{
		return false;
	}

	if (ENetworkRole::Server == m_role)
	{
		ServerPeer& peer = m_peers[id];
		if (ENetChannel::ReliableOrdered == channel)
		{
			const NetUdpEndpoint to = peer.Endpoint;
			const std::uint64_t  token = peer.Token;
			peer.Reliable.SendReliable(msgId, data, size, NowMs(),
				[this, to, token](UdpProto::UdpDatagramHeader& h, const void* p, std::uint32_t s)
				{
					h.Token = token;
					SendPacket(to, h, p, s);
				});
			return true;
		}
		const std::uint32_t seq = peer.SendSeq++;
		return SendDatagram(peer.Endpoint, peer.Token, channel, seq, msgId, data, size);
	}

	// 클라이언트.
	if (ENetChannel::ReliableOrdered == channel)
	{
		const NetUdpEndpoint to = m_serverEndpoint;
		const std::uint64_t  token = m_clientToken;
		m_clientReliable.SendReliable(msgId, data, size, NowMs(),
			[this, to, token](UdpProto::UdpDatagramHeader& h, const void* p, std::uint32_t s)
			{
				h.Token = token;
				SendPacket(to, h, p, s);
			});
		return true;
	}
	const std::uint32_t seq = m_clientSendSeq++;
	return SendDatagram(m_serverEndpoint, m_clientToken, channel, seq, msgId, data, size);
}

void CUdpChannel::SendPunch(NetworkConnectionId id)
{
	// punch = msgId 0 + payload 0. 엔드포인트 학습/NAT 유지용.
	if (false == CanSend(id, 0))
	{
		return;
	}
	if (ENetworkRole::Server == m_role)
	{
		ServerPeer& peer = m_peers[id];
		const std::uint32_t seq = peer.SendSeq++;
		SendDatagram(peer.Endpoint, peer.Token, ENetChannel::Unreliable, seq, 0, nullptr, 0);
	}
	else if (ENetworkRole::Client == m_role)
	{
		const std::uint32_t seq = m_clientSendSeq++;
		SendDatagram(m_serverEndpoint, m_clientToken, ENetChannel::Unreliable, seq, 0, nullptr, 0);
	}
}

bool CUdpChannel::SendPacket(
	const NetUdpEndpoint& to, UdpProto::UdpDatagramHeader& header, const void* data, std::uint32_t size)
{
	if (!m_socket)
	{
		return false;
	}
	std::vector<std::uint8_t> datagram;
	datagram.resize(UdpProto::HeaderSize(header.Flags) + size);
	const std::size_t written = UdpProto::Encode(header, data, size, datagram.data());
	return ESocketIo::Ok == m_socket->SendTo(to, datagram.data(), written);
}

bool CUdpChannel::SendDatagram(
	const NetUdpEndpoint& to, std::uint64_t token, ENetChannel channel,
	std::uint32_t seq, std::uint16_t msgId, const void* data, std::uint32_t size)
{
	UdpProto::UdpDatagramHeader header;
	header.Token   = token;
	header.Flags   = UdpProto::Flag_None; // 비신뢰 — ack/frag 없음.
	header.Channel = channel;
	header.Seq     = seq;
	header.MsgId   = msgId;
	return SendPacket(to, header, data, size);
}

double CUdpChannel::NowMs() const
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration<double, std::milli>(now).count();
}

// ── 수신 ─────────────────────────────────────────────────────────────────────────

void CUdpChannel::Poll(const FOnUdpMessage& onMessage)
{
	if (!m_socket)
	{
		return;
	}

	std::uint8_t buffer[UDP_RECV_BUFSIZE];
	for (;;)
	{
		std::size_t    bytes = 0;
		NetUdpEndpoint from;
		const ESocketIo result = m_socket->RecvFrom(buffer, sizeof(buffer), bytes, from);
		if (ESocketIo::Ok != result)
		{
			break; // WouldBlock / Error — 이번 폴 종료.
		}

		UdpProto::UdpDatagramHeader header;
		const std::uint8_t* payload = nullptr;
		std::uint32_t       psize   = 0;
		if (false == UdpProto::Decode(buffer, bytes, header, payload, psize))
		{
			continue; // 헤더 미달/잘림 — 폐기.
		}

		const std::uint64_t token   = header.Token;
		const ENetChannel   channel = header.Channel;
		const std::uint32_t seq     = header.Seq;
		const std::uint16_t msgId   = header.MsgId;

		if (ENetworkRole::Server == m_role)
		{
			const auto mapIt = m_tokenToPeer.find(token);
			if (m_tokenToPeer.end() == mapIt)
			{
				continue; // 미지의 토큰 — 무시(스푸핑 방어).
			}
			const NetworkConnectionId connId = mapIt->second;
			const auto peerIt = m_peers.find(connId);
			if (m_peers.end() == peerIt)
			{
				continue;
			}
			ServerPeer& peer = peerIt->second;
			peer.Endpoint = from; // 엔드포인트 학습/갱신(NAT 리바인딩 대응).

			// ack 필드는 신뢰 데이터·순수 ack 어느 쪽에도 실릴 수 있다 → 먼저 처리.
			if (0 != (header.Flags & UdpProto::Flag_Ack))
			{
				peer.Reliable.OnAck(header.AckBase, header.AckBits, NowMs());
			}
			if (0 != (header.Flags & UdpProto::Flag_Reliable))
			{
				if (peer.Reliable.OnReliableReceived(seq)) // dedup — 최초만 전달.
				{
					onMessage(connId, msgId, payload, psize);
				}
				continue; // 신뢰 seq 는 손실지표/Sequenced 대상 아님. ack 는 Tick 에서 flush.
			}
			if (0 != (header.Flags & UdpProto::Flag_Ack))
			{
				continue; // 순수 ack — 데이터 없음.
			}

			// 비신뢰 경로.
			peer.Stats.Accumulate(seq); // 손실률 표본(punch 포함).
			if (0 == msgId && 0 == psize)
			{
				continue; // punch — 디스패치 안 함.
			}
			if (AcceptSeq(peer.RecvLastSeq, channel, msgId, seq))
			{
				onMessage(connId, msgId, payload, psize);
			}
		}
		else if (ENetworkRole::Client == m_role)
		{
			if (false == m_clientTokenSet || token != m_clientToken)
			{
				continue; // 우리 연결 토큰 아님 — 무시.
			}

			if (0 != (header.Flags & UdpProto::Flag_Ack))
			{
				m_clientReliable.OnAck(header.AckBase, header.AckBits, NowMs());
			}
			if (0 != (header.Flags & UdpProto::Flag_Reliable))
			{
				if (m_clientReliable.OnReliableReceived(seq))
				{
					onMessage(SERVER_CONNECTION_ID, msgId, payload, psize);
				}
				continue;
			}
			if (0 != (header.Flags & UdpProto::Flag_Ack))
			{
				continue; // 순수 ack.
			}

			m_clientStats.Accumulate(seq); // 손실률 표본(punch 포함).
			if (0 == msgId && 0 == psize)
			{
				continue; // punch.
			}
			if (AcceptSeq(m_clientRecvLastSeq, channel, msgId, seq))
			{
				onMessage(SERVER_CONNECTION_ID, msgId, payload, psize);
			}
		}
	}

	// 수신 처리 후 신뢰 틱: RTO 만료 재전송 + 대기 ack flush(방금 받은 신뢰분 포함).
	const double now = NowMs();
	if (ENetworkRole::Server == m_role)
	{
		for (auto& entry : m_peers)
		{
			ServerPeer& peer = entry.second;
			if (false == peer.Endpoint.Valid())
			{
				continue;
			}
			const NetUdpEndpoint to    = peer.Endpoint;
			const std::uint64_t  token = peer.Token;
			peer.Reliable.Tick(now,
				[this, to, token](UdpProto::UdpDatagramHeader& h, const void* p, std::uint32_t s)
				{
					h.Token = token;
					SendPacket(to, h, p, s);
				});
		}
	}
	else if (ENetworkRole::Client == m_role && m_clientTokenSet && m_serverEndpoint.Valid())
	{
		const NetUdpEndpoint to    = m_serverEndpoint;
		const std::uint64_t  token = m_clientToken;
		m_clientReliable.Tick(now,
			[this, to, token](UdpProto::UdpDatagramHeader& h, const void* p, std::uint32_t s)
			{
				h.Token = token;
				SendPacket(to, h, p, s);
			});
	}
}

bool CUdpChannel::AcceptSeq(
	std::unordered_map<std::uint16_t, std::uint32_t>& lastSeqMap,
	ENetChannel channel, std::uint16_t msgId, std::uint32_t seq)
{
	if (ENetChannel::UnreliableSequenced != channel)
	{
		return true; // 순수 Unreliable — 순서 무시, 항상 전달.
	}

	const auto it = lastSeqMap.find(msgId);
	if (lastSeqMap.end() != it && seq <= it->second)
	{
		return false; // 오래된(역전) 패킷 — 폐기.
	}
	lastSeqMap[msgId] = seq;
	return true;
}

std::uint64_t CUdpChannel::NextToken()
{
	// xorshift64(비암호 — LAN 연결 매핑용). 0 은 회피.
	std::uint64_t x = m_rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	m_rng = x;
	return (0 != x) ? x : 0x9E3779B97F4A7C15ull;
}

#endif // !JBRO_PLATFORM_WEB
