#pragma once

#include "Core/Platform/PlatformDefines.h"
#if JBRO_PLATFORM_WINDOWS

#include "Core/Network/INetworkTransport.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

// ── Internal connection record (no Windows headers needed here) ────────────────

struct WinSockConnection
{
	// SOCKET is UINT_PTR on Windows — stored as uintptr_t to keep this header
	// free of <winsock2.h>.
	uintptr_t               Socket    = ~static_cast<uintptr_t>(0);
	ENetworkConnectionState State     = ENetworkConnectionState::Disconnected;
	std::vector<std::uint8_t> RecvBuffer;
};

// ── TCP transport using non-blocking WinSock2 sockets ─────────────────────────
//
// Wire format for each message:
//   [4 bytes, little-endian uint32 = payload length][payload bytes]
//
// Both client and server modes are supported on Windows.
// Use SERVER_CONNECTION_ID when sending from client → server.
class CWinSockTransport final : public INetworkTransport
{
public:
	CWinSockTransport();
	~CWinSockTransport() override;

	bool Connect    (const char* host, std::uint16_t port) override;
	bool Listen     (std::uint16_t port) override;
	void Close      () override;
	void CloseConnection(NetworkConnectionId id) override;

	bool                    IsListening()                                        const override;
	ENetworkConnectionState GetConnectionState(NetworkConnectionId id)           const override;

	bool Send  (NetworkConnectionId id, const void* data, std::uint32_t size)         override;
	void Update()                                                                      override;

	void SetOnConnected   (FOnTransportConnected    callback) override;
	void SetOnDisconnected(FOnTransportDisconnected callback) override;
	void SetOnData        (FOnTransportData         callback) override;

private:
	void AcceptPendingConnections();
	void PollConnection(NetworkConnectionId id, WinSockConnection& conn);
	void RemoveConnection(NetworkConnectionId id);
	bool SendRaw(uintptr_t socket, const void* data, std::uint32_t size);
	NetworkConnectionId AllocConnectionId();

private:
	static constexpr uintptr_t INVALID_SOCK = ~static_cast<uintptr_t>(0);

	uintptr_t m_listenSocket = INVALID_SOCK;
	std::unordered_map<NetworkConnectionId, WinSockConnection> m_connections;
	NetworkConnectionId m_nextId         = 1;
	bool                m_wsaInitialized = false;
	bool                m_isListening    = false;

	FOnTransportConnected    m_onConnected;
	FOnTransportDisconnected m_onDisconnected;
	FOnTransportData         m_onData;
};

#endif // JBRO_PLATFORM_WINDOWS
