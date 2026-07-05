#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/INetworkTransport.h"
#include "Core/Network/Sockets/ISocket.h"
#include "Core/Network/WebSocket/WebSocketProtocol.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 네이티브(Windows·추후 Android) WebSocket transport.
// ISocket(TCP) + WebSocket 코덱으로 브라우저와 동일한 RFC6455 와이어를 말한다.
//   - 서버: accept 한 연결마다 서버 핸드셰이크 수행 → 브라우저/네이티브 클라 모두 수용.
//   - 클라이언트: 클라 핸드셰이크 후 데이터 프레임 교환.
// 서버→클라 프레임은 언마스크, 클라→서버 프레임은 마스크(RFC6455 규약).
class CNativeWebSocketTransport final : public INetworkTransport
{
public:
	CNativeWebSocketTransport();
	~CNativeWebSocketTransport() override;

	bool Connect(const char* host, std::uint16_t port) override;
	bool Listen(std::uint16_t port) override;
	void Close() override;
	void CloseConnection(NetworkConnectionId id) override;

	bool                    IsListening() const override;
	ENetworkConnectionState GetConnectionState(NetworkConnectionId id) const override;

	bool Send(NetworkConnectionId id, const void* data, std::uint32_t size) override;
	void Update() override;

	void SetOnConnected(FOnTransportConnected callback) override;
	void SetOnDisconnected(FOnTransportDisconnected callback) override;
	void SetOnData(FOnTransportData callback) override;

private:
	enum class EPhase : std::uint8_t
	{
		TcpConnecting, // 클라: 논블로킹 connect 진행 중.
		Handshaking,   // WS 오프닝 핸드셰이크 교환 중.
		Open,          // 데이터 프레임 교환 가능.
	};

	struct Connection
	{
		OwnerPtr<ISocket>         Socket;
		EPhase                    Phase        = EPhase::TcpConnecting;
		bool                      IsServerSide = false; // true=서버가 수락한 클라 연결.
		bool                      Reported     = false; // OnConnected 이미 보고?

		std::vector<std::uint8_t> RecvBuffer;   // 소켓에서 읽은 원시 바이트.
		std::vector<std::uint8_t> SendBuffer;   // 송신 대기(부분전송 대비) 바이트.

		std::vector<std::uint8_t> FragmentData;   // 조립 중 메시지 페이로드.
		WebSocket::EOpcode        FragmentOpcode = WebSocket::EOpcode::Binary;
		bool                      InFragment     = false;

		// 클라 전용: 핸드셰이크 검증/전송 상태.
		std::string               ClientKey;
		std::string               HostHeader;
		bool                      HandshakeSent  = false;
	};

	void   AcceptPending();
	void   ServiceConnection(NetworkConnectionId id, Connection& conn);
	bool   ReadIntoRecvBuffer(NetworkConnectionId id, Connection& conn); // false=드롭됨.
	bool   PumpHandshake(NetworkConnectionId id, Connection& conn);      // false=드롭됨.
	bool   PumpFrames(NetworkConnectionId id, Connection& conn);         // false=드롭됨.
	void   FlushSendBuffer(Connection& conn);
	void   QueueFrame(Connection& conn, WebSocket::EOpcode opcode, const void* data, std::size_t size);
	void   RemoveConnection(NetworkConnectionId id);
	NetworkConnectionId AllocConnectionId();
	std::uint32_t       NextMaskKey();

private:
	OwnerPtr<ISocket>                                   m_listenSocket;
	bool                                                m_isListening = false;
	std::unordered_map<NetworkConnectionId, Connection> m_connections;
	NetworkConnectionId                                 m_nextId   = SERVER_CONNECTION_ID;
	std::uint32_t                                       m_maskRng  = 0x1234ABCDu;

	FOnTransportConnected    m_onConnected;
	FOnTransportDisconnected m_onDisconnected;
	FOnTransportData         m_onData;
};

#endif // !JBRO_PLATFORM_WEB
