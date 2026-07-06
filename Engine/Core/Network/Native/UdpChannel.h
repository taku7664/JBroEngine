#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/NetworkTypes.h"
#include "Core/Network/Sockets/IUdpSocket.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

// 네이티브 비신뢰(UDP) 채널. 신뢰 WS 연결 위에 얹혀 도는 부가 데이터그램 경로.
//   - 서버: TCP 와 동일 포트에 UDP bind. 연결마다 토큰 발급(신뢰 채널로 전달) → 토큰으로
//           인바운드 데이터그램을 연결에 매핑, 첫 수신 src 로 클라 엔드포인트 학습.
//   - 클라: 서버 토큰 수신 후 ephemeral UDP 로 서버에 송신. punch 로 NAT 유지/엔드포인트 등록.
//   - 웹은 UDP 불가라 이 계층 자체가 없다(#if). 미준비/대용량 메시지는 NM 이 신뢰로 폴백.
//
// 데이터그램: [token:8 LE][channel:1][seq:4 LE][msgId:2 LE][payload]
//   양방향 토큰 인증. punch = msgId 0 + payload 0(디스패치 안 함, 엔드포인트 등록용).
class CUdpChannel
{
public:
	// (connId, msgId, payload, size) — NM 이 기존 타입드/raw 경로로 디스패치.
	using FOnUdpMessage = std::function<void(NetworkConnectionId, std::uint16_t, const std::uint8_t*, std::uint32_t)>;

	bool StartServer(std::uint16_t port);
	bool StartClient(const char* serverHost, std::uint16_t serverPort);
	void Stop();

	// 서버: 연결 Ready 시 호출 → 토큰 발급(반환). NM 이 이를 신뢰로 클라에 전달.
	std::uint64_t RegisterServerPeer(NetworkConnectionId id);
	void          RemovePeer(NetworkConnectionId id);

	// 클라: 서버가 준 토큰 저장 + 즉시 punch(엔드포인트 등록 유도).
	void SetClientToken(std::uint64_t token);

	// UDP 로 보낼 수 있는가(엔드포인트/토큰 준비 + 크기 적정)?
	bool CanSend(NetworkConnectionId id, std::uint32_t payloadSize) const;

	// UDP 송신. 실패(미준비/대용량/오류) 시 false → NM 이 신뢰로 폴백.
	bool Send(NetworkConnectionId id, ENetChannel channel, std::uint16_t msgId, const void* data, std::uint32_t size);

	// 수신 폴 → 콜백 디스패치. 매 프레임 호출.
	void Poll(const FOnUdpMessage& onMessage);

	// NAT 유지/엔드포인트 등록용 빈 데이터그램.
	void SendPunch(NetworkConnectionId id);

	bool IsActive() const { return nullptr != m_socket.Get(); }

private:
	struct ServerPeer
	{
		std::uint64_t  Token = 0;
		NetUdpEndpoint Endpoint;                 // 첫 인바운드에서 학습.
		std::uint32_t  SendSeq = 0;
		std::unordered_map<std::uint16_t, std::uint32_t> RecvLastSeq; // msgId → 최근 seq(Sequenced).
	};

	std::uint64_t NextToken();
	bool SendDatagram(const NetUdpEndpoint& to, std::uint64_t token,
		ENetChannel channel, std::uint32_t seq, std::uint16_t msgId, const void* data, std::uint32_t size);
	// 수신 페이로드가 시퀀스 드롭 대상인지 판정 + lastSeq 갱신.
	bool AcceptSeq(std::unordered_map<std::uint16_t, std::uint32_t>& lastSeqMap,
		ENetChannel channel, std::uint16_t msgId, std::uint32_t seq);

private:
	OwnerPtr<IUdpSocket> m_socket;
	ENetworkRole         m_role = ENetworkRole::None;
	std::uint64_t        m_rng  = 0xD1B54A32D192ED03ull;

	// 서버측.
	std::unordered_map<NetworkConnectionId, ServerPeer> m_peers;
	std::unordered_map<std::uint64_t, NetworkConnectionId> m_tokenToPeer;

	// 클라측.
	bool           m_clientTokenSet = false;
	std::uint64_t  m_clientToken    = 0;
	NetUdpEndpoint m_serverEndpoint;
	std::uint32_t  m_clientSendSeq  = 0;
	std::unordered_map<std::uint16_t, std::uint32_t> m_clientRecvLastSeq;
};

#endif // !JBRO_PLATFORM_WEB
