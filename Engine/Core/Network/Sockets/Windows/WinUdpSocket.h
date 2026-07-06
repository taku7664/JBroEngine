#pragma once

#include "Core/Platform/PlatformDefines.h"
#if JBRO_PLATFORM_WINDOWS

#include "Core/Network/Sockets/IUdpSocket.h"

#include <cstdint>

// WinSock2 논블로킹 UDP 소켓. <winsock2.h> 는 .cpp 에만.
class CWinUdpSocket final : public IUdpSocket
{
public:
	CWinUdpSocket();
	~CWinUdpSocket() override;

	bool Open() override;
	bool Bind(std::uint16_t port) override;
	bool Resolve(const char* host, std::uint16_t port, NetUdpEndpoint& outEndpoint) override;
	ESocketIo SendTo(const NetUdpEndpoint& to, const void* data, std::size_t size) override;
	ESocketIo RecvFrom(void* buffer, std::size_t bufferSize, std::size_t& outBytes, NetUdpEndpoint& outFrom) override;
	void Close() override;
	bool IsValid() const override;

private:
	void SetNonBlocking();

private:
	static constexpr uintptr_t INVALID_SOCK = ~static_cast<uintptr_t>(0);
	uintptr_t m_socket = INVALID_SOCK;
};

#endif // JBRO_PLATFORM_WINDOWS
