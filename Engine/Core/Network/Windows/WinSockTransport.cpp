#include "pch.h"
#include "WinSockTransport.h"

#if JBRO_PLATFORM_WINDOWS

// Framework.h (included via pch.h) already defines WIN32_LEAN_AND_MEAN and
// includes <windows.h>.  WIN32_LEAN_AND_MEAN suppresses the old <winsock.h>,
// so including <winsock2.h> here is safe.
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstring>

// ── Wire-format constants ──────────────────────────────────────────────────────

static constexpr std::uint32_t FRAME_HEADER_SIZE = 4;           // bytes for length prefix
static constexpr std::uint32_t MAX_MESSAGE_SIZE  = 4 * 1024 * 1024; // 4 MB guard

// ── Helpers ────────────────────────────────────────────────────────────────────

static inline SOCKET   ToSocket  (uintptr_t v) { return static_cast<SOCKET>(v); }
static inline uintptr_t FromSocket(SOCKET    s) { return static_cast<uintptr_t>(s); }

// ── Constructor / destructor ───────────────────────────────────────────────────

CWinSockTransport::CWinSockTransport()
{
	WSADATA wsa;
	if (0 == WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		m_wsaInitialized = true;
	}
}

CWinSockTransport::~CWinSockTransport()
{
	Close();
	if (m_wsaInitialized)
	{
		WSACleanup();
		m_wsaInitialized = false;
	}
}

// ── INetworkTransport ──────────────────────────────────────────────────────────

bool CWinSockTransport::Connect(const char* host, std::uint16_t port)
{
	if (!m_wsaInitialized || !host)
	{
		return false;
	}

	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

	addrinfo hints = {};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* results = nullptr;
	if (0 != getaddrinfo(host, portStr, &hints, &results) || nullptr == results)
	{
		return false;
	}

	SOCKET s = socket(results->ai_family, results->ai_socktype, results->ai_protocol);
	if (INVALID_SOCKET == s)
	{
		freeaddrinfo(results);
		return false;
	}

	// Non-blocking mode
	u_long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);

	const int rc = connect(s, results->ai_addr, static_cast<int>(results->ai_addrlen));
	freeaddrinfo(results);

	if (0 != rc && WSAEWOULDBLOCK != WSAGetLastError())
	{
		closesocket(s);
		return false;
	}

	const NetworkConnectionId id = AllocConnectionId();
	WinSockConnection conn;
	conn.Socket = FromSocket(s);
	conn.State  = ENetworkConnectionState::Connecting;
	m_connections.emplace(id, std::move(conn));
	return true;
}

bool CWinSockTransport::Listen(std::uint16_t port)
{
	if (!m_wsaInitialized)
	{
		return false;
	}

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == s)
	{
		return false;
	}

	int optVal = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	           reinterpret_cast<const char*>(&optVal), sizeof(optVal));

	sockaddr_in addr    = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port        = htons(port);

	if (0 != bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
	{
		closesocket(s);
		return false;
	}

	if (0 != listen(s, SOMAXCONN))
	{
		closesocket(s);
		return false;
	}

	u_long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);

	m_listenSocket = FromSocket(s);
	m_isListening  = true;
	return true;
}

void CWinSockTransport::Close()
{
	// Collect IDs first to avoid iterator invalidation during removal.
	std::vector<NetworkConnectionId> ids;
	ids.reserve(m_connections.size());
	for (const auto& [id, _] : m_connections)
	{
		ids.push_back(id);
	}
	for (NetworkConnectionId id : ids)
	{
		RemoveConnection(id);
	}

	if (INVALID_SOCK != m_listenSocket)
	{
		closesocket(ToSocket(m_listenSocket));
		m_listenSocket = INVALID_SOCK;
	}
	m_isListening = false;
}

void CWinSockTransport::CloseConnection(NetworkConnectionId id)
{
	RemoveConnection(id);
}

bool CWinSockTransport::IsListening() const
{
	return m_isListening;
}

ENetworkConnectionState CWinSockTransport::GetConnectionState(NetworkConnectionId id) const
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it)
	{
		return ENetworkConnectionState::Disconnected;
	}
	return it->second.State;
}

bool CWinSockTransport::Send(NetworkConnectionId id, const void* data, std::uint32_t size)
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it ||
	    ENetworkConnectionState::Connected != it->second.State)
	{
		return false;
	}

	// Prepend 4-byte little-endian length header.
	const std::uint32_t len = size;
	if (false == SendRaw(it->second.Socket, &len, FRAME_HEADER_SIZE))
	{
		return false;
	}
	if (0 == size)
	{
		return true;
	}
	return SendRaw(it->second.Socket, data, size);
}

void CWinSockTransport::Update()
{
	if (m_isListening)
	{
		AcceptPendingConnections();
	}

	// Snapshot IDs — PollConnection may erase entries from m_connections.
	std::vector<NetworkConnectionId> ids;
	ids.reserve(m_connections.size());
	for (const auto& [id, _] : m_connections)
	{
		ids.push_back(id);
	}

	for (NetworkConnectionId id : ids)
	{
		auto it = m_connections.find(id);
		if (m_connections.end() != it)
		{
			PollConnection(id, it->second);
		}
	}
}

void CWinSockTransport::SetOnConnected(FOnTransportConnected callback)
{
	m_onConnected = std::move(callback);
}

void CWinSockTransport::SetOnDisconnected(FOnTransportDisconnected callback)
{
	m_onDisconnected = std::move(callback);
}

void CWinSockTransport::SetOnData(FOnTransportData callback)
{
	m_onData = std::move(callback);
}

// ── Private helpers ────────────────────────────────────────────────────────────

void CWinSockTransport::AcceptPendingConnections()
{
	SOCKET listenSock = ToSocket(m_listenSocket);
	for (;;)
	{
		sockaddr_in clientAddr = {};
		int         addrLen    = sizeof(clientAddr);
		SOCKET clientSock = accept(
			listenSock,
			reinterpret_cast<sockaddr*>(&clientAddr),
			&addrLen);

		if (INVALID_SOCKET == clientSock)
		{
			break; // No more pending connections right now.
		}

		u_long mode = 1;
		ioctlsocket(clientSock, FIONBIO, &mode);

		const NetworkConnectionId id = AllocConnectionId();
		WinSockConnection conn;
		conn.Socket = FromSocket(clientSock);
		conn.State  = ENetworkConnectionState::Connected;
		m_connections.emplace(id, std::move(conn));

		if (m_onConnected)
		{
			m_onConnected(id);
		}
	}
}

void CWinSockTransport::PollConnection(NetworkConnectionId id, WinSockConnection& conn)
{
	SOCKET s = ToSocket(conn.Socket);

	// ── Pending non-blocking connect ──────────────────────────────────────────
	if (ENetworkConnectionState::Connecting == conn.State)
	{
		fd_set writeset, errset;
		FD_ZERO(&writeset);
		FD_ZERO(&errset);
		FD_SET(s, &writeset);
		FD_SET(s, &errset);

		timeval tv = {0, 0};
		if (select(0, nullptr, &writeset, &errset, &tv) > 0)
		{
			if (FD_ISSET(s, &errset))
			{
				RemoveConnection(id);
				return;
			}
			if (FD_ISSET(s, &writeset))
			{
				conn.State = ENetworkConnectionState::Connected;
				if (m_onConnected)
				{
					m_onConnected(id);
				}
			}
		}
		return; // Don't attempt recv while still connecting.
	}

	// ── Non-blocking recv loop ─────────────────────────────────────────────────
	static constexpr int CHUNK = 4096;
	std::uint8_t tmp[CHUNK];

	for (;;)
	{
		const int n = recv(s, reinterpret_cast<char*>(tmp), CHUNK, 0);
		if (n > 0)
		{
			conn.RecvBuffer.insert(conn.RecvBuffer.end(), tmp, tmp + n);
		}
		else if (0 == n)
		{
			// Graceful close by the peer.
			RemoveConnection(id);
			return;
		}
		else
		{
			if (WSAEWOULDBLOCK == WSAGetLastError())
			{
				break; // No more data available right now.
			}
			RemoveConnection(id);
			return;
		}
	}

	// ── Dispatch complete framed messages ──────────────────────────────────────
	while (conn.RecvBuffer.size() >= FRAME_HEADER_SIZE)
	{
		std::uint32_t msgLen = 0;
		std::memcpy(&msgLen, conn.RecvBuffer.data(), FRAME_HEADER_SIZE);

		if (msgLen > MAX_MESSAGE_SIZE)
		{
			// Suspiciously large — close the connection.
			RemoveConnection(id);
			return;
		}

		const std::size_t total = FRAME_HEADER_SIZE + msgLen;
		if (conn.RecvBuffer.size() < total)
		{
			break; // Partial message — wait for more data.
		}

		if (m_onData)
		{
			m_onData(id, conn.RecvBuffer.data() + FRAME_HEADER_SIZE, msgLen);
		}

		conn.RecvBuffer.erase(
			conn.RecvBuffer.begin(),
			conn.RecvBuffer.begin() + static_cast<std::ptrdiff_t>(total));
	}
}

void CWinSockTransport::RemoveConnection(NetworkConnectionId id)
{
	const auto it = m_connections.find(id);
	if (m_connections.end() == it)
	{
		return;
	}

	if (INVALID_SOCK != it->second.Socket)
	{
		closesocket(ToSocket(it->second.Socket));
	}

	const ENetworkConnectionState prevState = it->second.State;
	m_connections.erase(it);

	if (ENetworkConnectionState::Disconnected != prevState && m_onDisconnected)
	{
		m_onDisconnected(id);
	}
}

bool CWinSockTransport::SendRaw(uintptr_t socket, const void* data, std::uint32_t size)
{
	SOCKET      s         = ToSocket(socket);
	const char* ptr       = static_cast<const char*>(data);
	std::uint32_t remaining = size;

	while (remaining > 0)
	{
		const int sent = send(s, ptr, static_cast<int>(remaining), 0);
		if (sent <= 0)
		{
			return false;
		}
		ptr       += sent;
		remaining -= static_cast<std::uint32_t>(sent);
	}
	return true;
}

NetworkConnectionId CWinSockTransport::AllocConnectionId()
{
	return m_nextId++;
}

#endif // JBRO_PLATFORM_WINDOWS
