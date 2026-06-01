#pragma once

#include "Core/Network/NetworkTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>

using FOnNetworkConnected    = std::function<void(NetworkConnectionId)>;
using FOnNetworkDisconnected = std::function<void(NetworkConnectionId)>;
using FOnNetworkDataReceived = std::function<void(NetworkConnectionId, const void*, std::uint32_t)>;

// High-level network manager exposed to game code via Core::Network.
// Platform differences (WinSock TCP vs. Emscripten WebSocket) are hidden
// behind this interface.
//
// Typical client usage:
//   Core::Network->SetOnConnected([](NetworkConnectionId id) { ... });
//   Core::Network->SetOnDataReceived([](NetworkConnectionId id, const void* data, uint32_t size) { ... });
//   Core::Network->Connect("127.0.0.1", 7777);
//   // Each frame the engine calls Update() automatically.
//   Core::Network->Send(SERVER_CONNECTION_ID, &msg, sizeof(msg));
//
// Typical server usage (Windows only):
//   Core::Network->StartServer(7777);
//   Core::Network->Broadcast(&msg, sizeof(msg));
class INetworkManager : public EnableSafeFromThis<INetworkManager>
{
public:
	virtual ~INetworkManager() = default;

	virtual bool Initialize() = 0;
	virtual void Finalize()   = 0;

	// Client: connect to host:port.
	virtual bool Connect(const char* host, std::uint16_t port) = 0;

	// Server: start listening.  Returns false on Web (browsers cannot listen).
	virtual bool StartServer(std::uint16_t port) = 0;

	// Disconnect everything and reset state.
	virtual void Disconnect() = 0;

	// Server: close one client connection.
	virtual void DisconnectClient(NetworkConnectionId id) = 0;

	virtual bool          IsConnected() const = 0;
	virtual bool          IsListening() const = 0;
	virtual ENetworkRole  GetRole()     const = 0;

	// Client: use SERVER_CONNECTION_ID to send to the server.
	// Server: specify a client's NetworkConnectionId.
	virtual bool Send(NetworkConnectionId id, const void* data, std::uint32_t size) = 0;

	// Server only: send the same message to all connected clients.
	virtual bool Broadcast(const void* data, std::uint32_t size) = 0;

	// Called automatically by the engine every frame.
	virtual void Update() = 0;

	virtual void SetOnConnected   (FOnNetworkConnected    callback) = 0;
	virtual void SetOnDisconnected(FOnNetworkDisconnected callback) = 0;
	virtual void SetOnDataReceived(FOnNetworkDataReceived callback) = 0;
};
