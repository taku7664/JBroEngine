#pragma once

#include "Core/Platform/PlatformDefines.h"
#if JBRO_PLATFORM_WINDOWS

#include "Core/Network/Sockets/ISocket.h"

#include <cstdint>

// WinSock2 논블로킹 TCP 소켓. <winsock2.h> 는 .cpp 에만 포함(헤더 청결).
// SOCKET 은 UINT_PTR 이므로 uintptr_t 로 보관한다.
class CWinTcpSocket final : public ISocket
{
public:
	CWinTcpSocket();
	// Accept 산출: 이미 연결된 소켓 핸들을 감싼다.
	explicit CWinTcpSocket(uintptr_t acceptedSocket);
	~CWinTcpSocket() override;

	bool Connect(const char* host, std::uint16_t port) override;
	bool Listen(std::uint16_t port) override;
	OwnerPtr<ISocket> Accept() override;
	ESocketConnect PollConnect() override;
	ESocketIo Recv(void* buffer, std::size_t size, std::size_t& outBytes) override;
	ESocketIo Send(const void* data, std::size_t size, std::size_t& outBytes) override;
	void Close() override;
	bool IsValid() const override;

private:
	bool EnsureSocket();
	void SetNonBlocking();

private:
	static constexpr uintptr_t INVALID_SOCK = ~static_cast<uintptr_t>(0);
	uintptr_t m_socket = INVALID_SOCK;
};

#endif // JBRO_PLATFORM_WINDOWS
