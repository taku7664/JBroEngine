#include "pch.h"
#include "NetworkManager.h"

#include <algorithm>

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
	if (m_transport)
	{
		m_transport->Close();
	}
	m_connections.clear();
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
	return m_transport->Connect(host, port);
}

bool CNetworkManager::StartServer(std::uint16_t port)
{
	if (!m_transport)
	{
		return false;
	}
	m_role = ENetworkRole::Server;
	return m_transport->Listen(port);
}

void CNetworkManager::Disconnect()
{
	if (m_transport)
	{
		m_transport->Close();
	}
	m_connections.clear();
	m_role = ENetworkRole::None;
}

void CNetworkManager::DisconnectClient(NetworkConnectionId id)
{
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

bool CNetworkManager::Send(NetworkConnectionId id, const void* data, std::uint32_t size)
{
	return m_transport && m_transport->Send(id, data, size);
}

bool CNetworkManager::Broadcast(const void* data, std::uint32_t size)
{
	if (!m_transport || m_connections.empty())
	{
		return false;
	}

	bool allOk = true;
	for (NetworkConnectionId id : m_connections)
	{
		if (false == m_transport->Send(id, data, size))
		{
			allOk = false;
		}
	}
	return allOk;
}

void CNetworkManager::Update()
{
	if (m_transport)
	{
		m_transport->Update();
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
	m_connections.push_back(id);
	if (m_onConnected)
	{
		m_onConnected(id);
	}
}

void CNetworkManager::OnTransportDisconnected(NetworkConnectionId id)
{
	m_connections.erase(
		std::remove(m_connections.begin(), m_connections.end(), id),
		m_connections.end());
	if (m_onDisconnected)
	{
		m_onDisconnected(id);
	}
}

void CNetworkManager::OnTransportData(
	NetworkConnectionId id, const std::uint8_t* data, std::uint32_t size)
{
	if (m_onDataReceived)
	{
		m_onDataReceived(id, data, size);
	}
}
