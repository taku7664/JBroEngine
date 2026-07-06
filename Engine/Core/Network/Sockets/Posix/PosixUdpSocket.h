#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB

#include "Core/Network/Sockets/IUdpSocket.h"

#include <cstdint>

// POSIX 논블로킹 UDP 소켓 — Android/iOS/Linux/macOS. WinUdpSocket 과 동일 계약.
// 주의: 아직 POSIX 툴체인에서 미검증(현 개발 머신 Windows). 윈도우 빌드에선 #if 배제.
class CPosixUdpSocket final : public IUdpSocket
{
public:
	CPosixUdpSocket() = default;
	~CPosixUdpSocket() override;

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
	int m_fd = -1;
};

#endif // !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB
