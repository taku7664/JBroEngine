#include "pch.h"
#include "PosixUdpSocket.h"

#if !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB

#include <cstdio>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
	inline bool WouldBlock() { return EWOULDBLOCK == errno || EAGAIN == errno; }
}

CPosixUdpSocket::~CPosixUdpSocket()
{
	Close();
}

bool CPosixUdpSocket::Open()
{
	if (-1 != m_fd)
	{
		return true;
	}
	m_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (-1 == m_fd)
	{
		return false;
	}
	SetNonBlocking();
	return true;
}

void CPosixUdpSocket::SetNonBlocking()
{
	if (-1 == m_fd)
	{
		return;
	}
	const int flags = fcntl(m_fd, F_GETFL, 0);
	fcntl(m_fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
}

bool CPosixUdpSocket::Bind(std::uint16_t port)
{
	if (-1 == m_fd && false == Open())
	{
		return false;
	}

	sockaddr_in addr    = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port        = htons(port);

	return 0 == bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

bool CPosixUdpSocket::Resolve(const char* host, std::uint16_t port, NetUdpEndpoint& outEndpoint)
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

ESocketIo CPosixUdpSocket::SendTo(const NetUdpEndpoint& to, const void* data, std::size_t size)
{
	if (-1 == m_fd || false == to.Valid())
	{
		return ESocketIo::Error;
	}

	const ssize_t sent = sendto(m_fd, data, size, 0,
		reinterpret_cast<const sockaddr*>(to.Data), static_cast<socklen_t>(to.Length));
	if (sent >= 0)
	{
		return ESocketIo::Ok;
	}
	if (WouldBlock())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

ESocketIo CPosixUdpSocket::RecvFrom(void* buffer, std::size_t bufferSize, std::size_t& outBytes, NetUdpEndpoint& outFrom)
{
	outBytes = 0;
	if (-1 == m_fd)
	{
		return ESocketIo::Error;
	}

	socklen_t fromLen = sizeof(outFrom.Data);
	const ssize_t n = recvfrom(m_fd, buffer, bufferSize, 0,
		reinterpret_cast<sockaddr*>(outFrom.Data), &fromLen);
	if (n >= 0)
	{
		outBytes       = static_cast<std::size_t>(n);
		outFrom.Length = static_cast<std::uint32_t>(fromLen);
		return ESocketIo::Ok;
	}
	if (WouldBlock())
	{
		return ESocketIo::WouldBlock;
	}
	// UDP 에서 ICMP port-unreachable 등은 치명 아님 — 다음 폴로.
	if (ECONNREFUSED == errno)
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

void CPosixUdpSocket::Close()
{
	if (-1 != m_fd)
	{
		close(m_fd);
		m_fd = -1;
	}
}

bool CPosixUdpSocket::IsValid() const
{
	return -1 != m_fd;
}

OwnerPtr<IUdpSocket> CreateUdpSocket()
{
	return MakeOwnerPtr<CPosixUdpSocket>();
}

#endif // !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB
