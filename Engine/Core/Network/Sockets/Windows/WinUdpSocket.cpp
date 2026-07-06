#include "pch.h"
#include "WinUdpSocket.h"

#if JBRO_PLATFORM_WINDOWS

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <cstdio>

namespace
{
	// WSAStartup 은 OS 가 refcount 하므로 TCP 소켓과 별개 카운터여도 안전(매칭 Cleanup).
	std::atomic<int> g_udpWsaRefCount{ 0 };

	void WsaAcquire()
	{
		if (g_udpWsaRefCount.fetch_add(1) == 0)
		{
			WSADATA wsa;
			WSAStartup(MAKEWORD(2, 2), &wsa);
		}
	}

	void WsaRelease()
	{
		if (g_udpWsaRefCount.fetch_sub(1) == 1)
		{
			WSACleanup();
		}
	}

	inline SOCKET   ToSocket  (uintptr_t v) { return static_cast<SOCKET>(v); }
	inline uintptr_t FromSocket(SOCKET    s) { return static_cast<uintptr_t>(s); }
}

CWinUdpSocket::CWinUdpSocket()
{
	WsaAcquire();
}

CWinUdpSocket::~CWinUdpSocket()
{
	Close();
	WsaRelease();
}

bool CWinUdpSocket::Open()
{
	if (INVALID_SOCK != m_socket)
	{
		return true;
	}
	const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == s)
	{
		return false;
	}
	m_socket = FromSocket(s);
	SetNonBlocking();
	return true;
}

void CWinUdpSocket::SetNonBlocking()
{
	if (INVALID_SOCK == m_socket)
	{
		return;
	}
	u_long mode = 1;
	ioctlsocket(ToSocket(m_socket), FIONBIO, &mode);
}

bool CWinUdpSocket::Bind(std::uint16_t port)
{
	if (INVALID_SOCK == m_socket && false == Open())
	{
		return false;
	}

	sockaddr_in addr    = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port        = htons(port);

	if (0 != bind(ToSocket(m_socket), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
	{
		return false;
	}
	return true;
}

bool CWinUdpSocket::Resolve(const char* host, std::uint16_t port, NetUdpEndpoint& outEndpoint)
{
	if (nullptr == host)
	{
		return false;
	}

	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

	addrinfo hints = {};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	addrinfo* results = nullptr;
	if (0 != getaddrinfo(host, portStr, &hints, &results) || nullptr == results)
	{
		return false;
	}

	if (results->ai_addrlen > sizeof(outEndpoint.Data))
	{
		freeaddrinfo(results);
		return false;
	}

	std::memcpy(outEndpoint.Data, results->ai_addr, results->ai_addrlen);
	outEndpoint.Length = static_cast<std::uint32_t>(results->ai_addrlen);
	freeaddrinfo(results);
	return true;
}

ESocketIo CWinUdpSocket::SendTo(const NetUdpEndpoint& to, const void* data, std::size_t size)
{
	if (INVALID_SOCK == m_socket || false == to.Valid())
	{
		return ESocketIo::Error;
	}

	const int sent = sendto(
		ToSocket(m_socket), static_cast<const char*>(data), static_cast<int>(size), 0,
		reinterpret_cast<const sockaddr*>(to.Data), static_cast<int>(to.Length));

	if (sent >= 0)
	{
		return ESocketIo::Ok;
	}
	if (WSAEWOULDBLOCK == WSAGetLastError())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

ESocketIo CWinUdpSocket::RecvFrom(void* buffer, std::size_t bufferSize, std::size_t& outBytes, NetUdpEndpoint& outFrom)
{
	outBytes = 0;
	if (INVALID_SOCK == m_socket)
	{
		return ESocketIo::Error;
	}

	int fromLen = sizeof(outFrom.Data);
	const int n = recvfrom(
		ToSocket(m_socket), static_cast<char*>(buffer), static_cast<int>(bufferSize), 0,
		reinterpret_cast<sockaddr*>(outFrom.Data), &fromLen);

	if (n >= 0)
	{
		outBytes        = static_cast<std::size_t>(n);
		outFrom.Length  = static_cast<std::uint32_t>(fromLen);
		return ESocketIo::Ok;
	}
	if (WSAEWOULDBLOCK == WSAGetLastError())
	{
		return ESocketIo::WouldBlock;
	}
	// UDP 에서 ICMP port-unreachable 은 WSAECONNRESET 로 표면화될 수 있음 — 치명 아님.
	if (WSAECONNRESET == WSAGetLastError())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

void CWinUdpSocket::Close()
{
	if (INVALID_SOCK != m_socket)
	{
		closesocket(ToSocket(m_socket));
		m_socket = INVALID_SOCK;
	}
}

bool CWinUdpSocket::IsValid() const
{
	return INVALID_SOCK != m_socket;
}

OwnerPtr<IUdpSocket> CreateUdpSocket()
{
	return MakeOwnerPtr<CWinUdpSocket>();
}

#endif // JBRO_PLATFORM_WINDOWS
