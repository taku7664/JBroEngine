#pragma once

#include "Core/Network/INetworkManager.h"
#include "Core/Network/INetworkTransport.h"
#include "Core/Platform/PlatformDefines.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#if !JBRO_PLATFORM_WEB
class CUdpChannel; // 비신뢰 UDP 채널(네이티브 전용). 웹은 UDP 불가라 없음.
#endif

class CNetworkManager final : public INetworkManager
{
public:
	explicit CNetworkManager(OwnerPtr<INetworkTransport> transport);
	~CNetworkManager() override;

	bool Initialize() override;
	void Finalize()   override;

	bool Connect    (const char* host, std::uint16_t port) override;
	bool StartServer(std::uint16_t port) override;
	void Disconnect() override;
	void DisconnectClient(NetworkConnectionId id) override;

	bool          IsConnected() const override;
	bool          IsListening() const override;
	ENetworkRole  GetRole()     const override;
	double        GetRoundTripMs(NetworkConnectionId id) const override;

	void SetSecureServerCertificate(void* certContext) override;
	void SetSecureClientOptions(const char* hostName, bool skipCertValidation) override;

	bool Send     (NetworkConnectionId id, const void* data, std::uint32_t size) override;
	bool Broadcast(const void* data, std::uint32_t size) override;

	// 베이스의 타입드 파사드(Send<T>/Broadcast<T>)가 위 오버라이드에 가려지지 않게 노출.
	using INetworkManager::Send;
	using INetworkManager::Broadcast;

	void Update() override;

	void SetOnConnected   (FOnNetworkConnected    callback) override;
	void SetOnDisconnected(FOnNetworkDisconnected callback) override;
	void SetOnDataReceived(FOnNetworkDataReceived callback) override;

	void          RegisterMessageType   (const void* typeTag, std::uint16_t messageId) override;
	std::uint16_t MessageIdForType      (const void* typeTag) const override;
	void          RegisterMessageHandler(std::uint16_t messageId, FNetworkMessageHandler handler) override;
	bool          SendMessageBytes      (NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel) override;
	bool          BroadcastMessageBytes (std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel) override;

private:
	// [uint16 LE messageId][payload] 로 프레이밍해 transport 로 전송.
	// channel 은 전달보장 선택 — 현재 전 채널 신뢰 WS. UDP 백엔드 추가 시 여기서 분기.
	bool SendFramed(NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel);
	void OnTransportConnected   (NetworkConnectionId id);
	void OnTransportDisconnected(NetworkConnectionId id);
	void OnTransportData        (NetworkConnectionId id, const std::uint8_t* data, std::uint32_t size);
	// 유저(raw/타입드) 메시지 디스패치 — 신뢰 경로와 UDP 경로 공용.
	void DispatchUserMessage    (NetworkConnectionId id, std::uint16_t messageId, const std::uint8_t* payload, std::uint32_t size);

	// ── 세션 계층(hello/keepalive/RTT) ─────────────────────────────────────────
	enum class ESessionState : std::uint8_t
	{
		Handshaking, // hello 교환 중 — 게임에 아직 연결 통보 안 함.
		Ready,       // 버전 합의 완료 — 게임 OnConnected 발화됨.
	};

	struct ConnectionSession
	{
		ESessionState State          = ESessionState::Handshaking;
		double        LastRecvMs     = 0.0;  // 마지막 수신 시각(liveness).
		double        LastPingSentMs = 0.0;  // 마지막 ping 송신 시각.
		double        RoundTripMs    = -1.0; // 최근 RTT(미측정 -1).
		ENetworkDisconnectReason PendingReason = ENetworkDisconnectReason::Normal; // 로컬 종료 사유.
		bool          WantsClose     = false; // 지연 종료 플래그(재진입 안전 위해 Update 말미에 처리).
	};

	double NowMs() const;
	void   PromoteToReady(NetworkConnectionId id);
	void   DropConnection(NetworkConnectionId id, ENetworkDisconnectReason reason);
	bool   HandleSystemMessage(NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size);
	void   UpdateSessions();
	template<typename T>
	void   SendSystem(NetworkConnectionId id, std::uint16_t messageId, const T& message);

private:
	OwnerPtr<INetworkTransport>    m_transport;
	ENetworkRole                   m_role = ENetworkRole::None;
	std::vector<NetworkConnectionId> m_connections;
	std::unordered_map<NetworkConnectionId, ConnectionSession> m_sessions;

	// 클라가 Connect 에 쓴 서버 주소 — UDP 클라 소켓이 서버 UDP 엔드포인트를 만들 때 재사용.
	std::string   m_serverHost;
	std::uint16_t m_serverPort = 0;

#if !JBRO_PLATFORM_WEB
	OwnerPtr<CUdpChannel> m_udp; // 비신뢰 채널. 서버/클라 준비되면 생성. 웹엔 없음.
#endif

	FOnNetworkConnected    m_onConnected;
	FOnNetworkDisconnected m_onDisconnected;
	FOnNetworkDataReceived m_onDataReceived;

	std::unordered_map<const void*, std::uint16_t>     m_typeIds;        // 타입 태그 → 메시지 ID.
	std::unordered_map<std::uint16_t, FNetworkMessageHandler> m_messageHandlers; // 메시지 ID → 핸들러.

	bool m_isInitialized = false;
};
