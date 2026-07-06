#pragma once

#include "Core/Network/NetworkTypes.h"

#include <cstdint>
#include <functional>

using FOnTransportConnected    = std::function<void(NetworkConnectionId)>;
using FOnTransportDisconnected = std::function<void(NetworkConnectionId)>;
using FOnTransportData         = std::function<void(NetworkConnectionId, const std::uint8_t*, std::uint32_t)>;

// Low-level, message-oriented transport.
// TCP-based implementations (WinSock) add 4-byte length-prefix framing to
// become message-based.  WebSocket implementations are natively message-based
// so no framing is needed.
class INetworkTransport
{
public:
	virtual ~INetworkTransport() = default;

	// Client: begin connecting (non-blocking).
	// Connection result arrives via the OnConnected / OnDisconnected callbacks.
	virtual bool Connect(const char* host, std::uint16_t port) = 0;

	// Server: start listening on a local port.
	// Not supported on Web — always returns false on that platform.
	virtual bool Listen(std::uint16_t port) = 0;

	// Close all connections and release socket resources.
	virtual void Close() = 0;

	// Close one specific connection (server mode).
	virtual void CloseConnection(NetworkConnectionId id) = 0;

	virtual bool                     IsListening() const = 0;
	virtual ENetworkConnectionState  GetConnectionState(NetworkConnectionId id) const = 0;

	// Send a complete message to a connection.
	virtual bool Send(NetworkConnectionId id, const void* data, std::uint32_t size) = 0;

	// Poll I/O and fire callbacks.  Must be called every frame.
	// On Emscripten/WebSocket this is a no-op (callbacks are event-driven).
	virtual void Update() = 0;

	virtual void SetOnConnected   (FOnTransportConnected    callback) = 0;
	virtual void SetOnDisconnected(FOnTransportDisconnected callback) = 0;
	virtual void SetOnData        (FOnTransportData         callback) = 0;

	// wss(TLS) 설정 — 네이티브 전용. 웹/기타는 no-op(브라우저가 wss 를 직접 처리).
	//   SetSecureClient: Connect 전에 호출하면 wss(클라 TLS). skip 은 self-signed dev 용.
	//   SetSecureServer: 인증서(PCCERT_CONTEXT, void*)를 주면 수락 연결을 TLS 로 수용.
	virtual void SetSecureClient(const char* hostName, bool skipCertValidation) { (void)hostName; (void)skipCertValidation; }
	virtual void SetSecureServer(void* certContext) { (void)certContext; }
};
