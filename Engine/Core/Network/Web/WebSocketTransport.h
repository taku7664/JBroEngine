#pragma once

#include "Core/Platform/PlatformDefines.h"
#if JBRO_PLATFORM_WEB

#include "Core/Network/INetworkTransport.h"

#include <emscripten/websocket.h>

// ── Emscripten WebSocket transport (client-only) ───────────────────────────────
//
// Browsers cannot act as TCP servers, so Listen() always returns false.
// Callbacks are fired from Emscripten's event loop — Update() is a no-op.
//
// The server connection is always assigned SERVER_CONNECTION_ID (= 1).
// Messages are binary and sent as-is (no length-prefix framing needed because
// WebSocket is already message-framed).
//
// host passed to Connect() may be:
//   - A full WebSocket URL:  "ws://hostname:port/path"
//   - A bare hostname:       "hostname"  → becomes "ws://hostname:port"
class CWebSocketTransport final : public INetworkTransport
{
public:
	CWebSocketTransport();
	~CWebSocketTransport() override;

	bool Connect    (const char* host, std::uint16_t port) override;
	bool Listen     (std::uint16_t) override { return false; } // not supported on Web
	void Close      () override;
	void CloseConnection(NetworkConnectionId id) override;

	bool                    IsListening()                              const override { return false; }
	ENetworkConnectionState GetConnectionState(NetworkConnectionId id) const override;

	bool Send  (NetworkConnectionId id, const void* data, std::uint32_t size) override;
	void Update() override {} // callback-driven; nothing to poll

	void SetOnConnected   (FOnTransportConnected    callback) override;
	void SetOnDisconnected(FOnTransportDisconnected callback) override;
	void SetOnData        (FOnTransportData         callback) override;

private:
	static EM_BOOL OnOpen   (int, const EmscriptenWebSocketOpenEvent*,    void* userData);
	static EM_BOOL OnMessage(int, const EmscriptenWebSocketMessageEvent*, void* userData);
	static EM_BOOL OnClose  (int, const EmscriptenWebSocketCloseEvent*,   void* userData);
	static EM_BOOL OnError  (int, const EmscriptenWebSocketErrorEvent*,   void* userData);

private:
	EMSCRIPTEN_WEBSOCKET_T  m_socket       = 0;
	NetworkConnectionId     m_connectionId = INVALID_CONNECTION_ID;
	ENetworkConnectionState m_state        = ENetworkConnectionState::Disconnected;

	FOnTransportConnected    m_onConnected;
	FOnTransportDisconnected m_onDisconnected;
	FOnTransportData         m_onData;
};

#endif // JBRO_PLATFORM_WEB
