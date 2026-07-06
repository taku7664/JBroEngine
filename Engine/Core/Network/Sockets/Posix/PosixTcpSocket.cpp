#include "pch.h"
#include "PosixTcpSocket.h"

#if !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB

#include <cstdio>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
	inline bool WouldBlock() { return EWOULDBLOCK == errno || EAGAIN == errno; }
}

CPosixTcpSocket::CPosixTcpSocket(int acceptedFd)
	: m_fd(acceptedFd)
{
	SetNonBlocking();
}

CPosixTcpSocket::~CPosixTcpSocket()
{
	Close();
}

bool CPosixTcpSocket::EnsureSocket()
{
	if (-1 != m_fd)
	{
		return true;
	}
	m_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (-1 == m_fd)
	{
		return false;
	}
	SetNonBlocking();
	return true;
}

void CPosixTcpSocket::SetNonBlocking()
{
	if (-1 == m_fd)
	{
		return;
	}
	const int flags = fcntl(m_fd, F_GETFL, 0);
	fcntl(m_fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
}

bool CPosixTcpSocket::Connect(const char* host, std::uint16_t port)
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

	const int rc = connect(m_fd, results->ai_addr, static_cast<socklen_t>(results->ai_addrlen));
	freeaddrinfo(results);

	if (0 != rc && EINPROGRESS != errno && !WouldBlock())
	{
		Close();
		return false;
	}
	return true;
}

bool CPosixTcpSocket::Listen(std::uint16_t port)
{
	if (!EnsureSocket())
	{
		return false;
	}

	int optVal = 1;
	setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

	sockaddr_in addr    = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port        = htons(port);

	if (0 != bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
	{
		Close();
		return false;
	}
	if (0 != listen(m_fd, SOMAXCONN))
	{
		Close();
		return false;
	}
	return true;
}

OwnerPtr<ISocket> CPosixTcpSocket::Accept()
{
	if (-1 == m_fd)
	{
		return nullptr;
	}

	sockaddr_in clientAddr = {};
	socklen_t   addrLen    = sizeof(clientAddr);
	const int clientFd = accept(m_fd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
	if (-1 == clientFd)
	{
		return nullptr; // 대기 연결 없음(EWOULDBLOCK) 또는 오류 — 둘 다 nullptr.
	}
	return MakeOwnerPtr<CPosixTcpSocket>(clientFd);
}

ESocketConnect CPosixTcpSocket::PollConnect()
{
	if (-1 == m_fd)
	{
		return ESocketConnect::Failed;
	}

	fd_set writeSet;
	fd_set errSet;
	FD_ZERO(&writeSet);
	FD_ZERO(&errSet);
	FD_SET(m_fd, &writeSet);
	FD_SET(m_fd, &errSet);

	timeval tv = { 0, 0 };
	const int rc = select(m_fd + 1, nullptr, &writeSet, &errSet, &tv);
	if (rc <= 0)
	{
		return ESocketConnect::Pending;
	}
	if (FD_ISSET(m_fd, &errSet))
	{
		return ESocketConnect::Failed;
	}
	if (FD_ISSET(m_fd, &writeSet))
	{
		// 연결 완료 여부를 SO_ERROR 로 확정.
		int soErr = 0;
		socklen_t len = sizeof(soErr);
		if (0 == getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soErr, &len) && 0 == soErr)
		{
			return ESocketConnect::Connected;
		}
		return ESocketConnect::Failed;
	}
	return ESocketConnect::Pending;
}

ESocketIo CPosixTcpSocket::Recv(void* buffer, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (-1 == m_fd || 0 == size)
	{
		return ESocketIo::Error;
	}

	const ssize_t n = recv(m_fd, buffer, size, 0);
	if (n > 0)
	{
		outBytes = static_cast<std::size_t>(n);
		return ESocketIo::Ok;
	}
	if (0 == n)
	{
		return ESocketIo::Closed;
	}
	if (WouldBlock())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

ESocketIo CPosixTcpSocket::Send(const void* data, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (-1 == m_fd)
	{
		return ESocketIo::Error;
	}
	if (0 == size)
	{
		return ESocketIo::Ok;
	}

	// SIGPIPE 회피(MSG_NOSIGNAL). macOS 는 미지원일 수 있어 0 폴백.
#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;
#endif
	const ssize_t n = send(m_fd, data, size, flags);
	if (n > 0)
	{
		outBytes = static_cast<std::size_t>(n);
		return ESocketIo::Ok;
	}
	if (WouldBlock())
	{
		return ESocketIo::WouldBlock;
	}
	return ESocketIo::Error;
}

void CPosixTcpSocket::Close()
{
	if (-1 != m_fd)
	{
		close(m_fd);
		m_fd = -1;
	}
}

bool CPosixTcpSocket::IsValid() const
{
	return -1 != m_fd;
}

// ── 팩토리(POSIX 구현) ───────────────────────────────────────────────────────────
OwnerPtr<ISocket> CreateTcpSocket()
{
	return MakeOwnerPtr<CPosixTcpSocket>();
}

#endif // !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB
