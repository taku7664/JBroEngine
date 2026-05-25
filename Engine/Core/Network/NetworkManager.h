#pragma once

#include "Core/Network/INetworkManager.h"
#include "Core/Network/INetworkTransport.h"

#include <vector>

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

	bool Send     (NetworkConnectionId id, const void* data, std::uint32_t size) override;
	bool Broadcast(const void* data, std::uint32_t size) override;

	void Update() override;

	void SetOnConnected   (FOnNetworkConnected    callback) override;
	void SetOnDisconnected(FOnNetworkDisconnected callback) override;
	void SetOnDataReceived(FOnNetworkDataReceived callback) override;

private:
	void OnTransportConnected   (NetworkConnectionId id);
	void OnTransportDisconnected(NetworkConnectionId id);
	void OnTransportData        (NetworkConnectionId id, const std::uint8_t* data, std::uint32_t size);

private:
	OwnerPtr<INetworkTransport>    m_transport;
	ENetworkRole                   m_role = ENetworkRole::None;
	std::vector<NetworkConnectionId> m_connections;

	FOnNetworkConnected    m_onConnected;
	FOnNetworkDisconnected m_onDisconnected;
	FOnNetworkDataReceived m_onDataReceived;

	bool m_isInitialized = false;
};
