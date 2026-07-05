#include "pch.h"
#include "WinTcpSocket.h"

#if JBRO_PLATFORM_WINDOWS

// pch.h(Framework.h) 가 WIN32_LEAN_AND_MEAN 정의 + <windows.h> 포함 → 구 <winsock.h> 억제됨.
// 따라서 <winsock2.h> 를 여기서 포함해도 안전.
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <cstdio>

namespace
{
	// 프로세스 전역 WSAStartup refcount — 첫 소켓에서 시작, 마지막에서 정리.
	std::atomic<int> g_wsaRefCount{ 0 };

	void WsaAcquire()
	{
		if (g_wsaRefCount.fetch_add(1) == 0)
		{
			WSADATA wsa;
			WSAStartup(MAKEWORD(2, 2), &wsa);
		}
	}

	void WsaRelease()
	{
		if (g_wsaRefCount.fetch_sub(1) == 1)
		{
			WSACleanup();
		}
	}

	inline SOCKET   ToSocket  (uintptr_t v) { return static_cast<SOCKET>(v); }
	inline uintptr_t FromSocket(SOCKET    s) { return static_cast<uintptr_t>(s); }
}

CWinTcpSocket::CWinTcpSocket()
{
	WsaAcquire();
}

CWinTcpSocket::CWinTcpSocket(uintptr_t acceptedSocket)
	: m_socket(acceptedSocket)
{
	WsaAcquire();
	SetNonBlocking();
}

CWinTcpSocket::~CWinTcpSocket()
{
	Close();
	WsaRelease();
}

bool CWinTcpSocket::EnsureSocket()
{
	if (INVALID_SOCK != m_socket)
	{
		return true;
	}
	const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == s)
	{
		return false;
	}
	m_socket = FromSocket(s);
	SetNonBlocking();
	return true;
}

void CWinTcpSocket::SetNonBlocking()
{
	if (INVALID_SOCK == m_socket)
	{
		return;
	}
	u_long mode = 1;
	ioctlsocket(ToSocket(m_socket), FIONBIO, &mode);
}

bool CWinTcpSocket::Connect(const char* host, std::uint16_t port)
{
	if (nullptr == host)
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

	if (!EnsureSocket())
	{
		freeaddrinfo(results);
		return false;
	}

	const int rc = connect(ToSocket(m_socket), results->ai_addr, static_cast<int>(results->ai_addrlen));
	freeaddrinfo(results);

	if (0 != rc && WSAEWOULDBLOCK != WSAGetLastError())
	{
		Close();
		return false;
	}
	return true;
}

bool CWinTcpSocket::Listen(std::uint16_t port)
{
	if (!EnsureSocket())
	{
		return false;
	}

	int optVal = 1;
	setsockopt(ToSocket(m_socket), SOL_SOCKET, SO_REUSEADDR,
		reinterpret_cast<const char*>(&optVal), sizeof(optVal));

	sockaddr_in addr    = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port        = htons(port);

	if (0 != bind(ToSocket(m_socket), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
	{
		Close();
		return false;
	}
	if (0 != listen(ToSocket(m_socket), SOMAXCONN))
	{
		Close();
		return false;
	}
	return true;
}

OwnerPtr<ISocket> CWinTcpSocket::Accept()
{
	if (INVALID_SOCK == m_socket)
	{
		return nullptr;
	}

	sockaddr_in clientAddr = {};
	int         addrLen    = sizeof(clientAddr);
	const SOCKET clientSock = accept(
		ToSocket(m_socket), reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

	if (INVALID_SOCKET == clientSock)
	{
		return nullptr; // 대기 연결 없음(WSAEWOULDBLOCK) 또는 오류 — 둘 다 nullptr.
	}

	return MakeOwnerPtr<CWinTcpSocket>(FromSocket(clientSock));
}

ESocketConnect CWinTcpSocket::PollConnect()
{
	if (INVALID_SOCK == m_socket)
	{
		return ESocketConnect::Failed;
	}

	fd_set writeSet;
	fd_set errSet;
	FD_ZERO(&writeSet);
	FD_ZERO(&errSet);
	FD_SET(ToSocket(m_socket), &writeSet);
	FD_SET(ToSocket(m_socket), &errSet);

	timeval tv = { 0, 0 };
	const int rc = select(0, nullptr, &writeSet, &errSet, &tv);
	if (rc <= 0)
	{
		return ESocketConnect::Pending; // 아직 결정 안 됨.
	}
	if (FD_ISSET(ToSocket(m_socket), &errSet))
	{
		return ESocketConnect::Failed;
	}
	if (FD_ISSET(ToSocket(m_socket), &writeSet))
	{
		return ESocketConnect::Connected;
	}
	return ESocketConnect::Pending;
}

ESocketIo CWinTcpSocket::Recv(void* buffer, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (INVALID_SOCK == m_socket || 0 == size)
	{
		return ESocketIo::Error;
	}

	const int n = recv(ToSocket(m_socket), static_cast<char*>(buffer),
		static_cast<int>(size), 0);
	if (n > 0)
	{
		outBytes = static_cast<std::size_t>(n);
		return ESocketIo::Ok;
	}
	if (0 == n)
	{
		return ESocketIo::Closed;
	}
	if (WSAEWOULDBLOCK == WSAGetLastError())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

ESocketIo CWinTcpSocket::Send(const void* data, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (INVALID_SOCK == m_socket)
	{
		return ESocketIo::Error;
	}
	if (0 == size)
	{
		return ESocketIo::Ok;
	}

	const int n = send(ToSocket(m_socket), static_cast<const char*>(data),
		static_cast<int>(size), 0);
	if (n > 0)
	{
		outBytes = static_cast<std::size_t>(n);
		return ESocketIo::Ok;
	}
	if (WSAEWOULDBLOCK == WSAGetLastError())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

void CWinTcpSocket::Close()
{
	if (INVALID_SOCK != m_socket)
	{
		closesocket(ToSocket(m_socket));
		m_socket = INVALID_SOCK;
	}
}

bool CWinTcpSocket::IsValid() const
{
	return INVALID_SOCK != m_socket;
}

// ── 팩토리(Windows 구현) ────────────────────────────────────────────────────────
OwnerPtr<ISocket> CreateTcpSocket()
{
	return MakeOwnerPtr<CWinTcpSocket>();
}

#endif // JBRO_PLATFORM_WINDOWS
