#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB

#include "Core/Network/Sockets/ISocket.h"

#include <cstdint>

// POSIX(BSD 소켓) 논블로킹 TCP 소켓 — Android/iOS/Linux/macOS.
// WinTcpSocket 과 동일 계약, API 만 POSIX(socket/close/fcntl/errno).
//
// 주의: 이 구현은 아직 실제 POSIX 툴체인에서 컴파일·검증되지 않았다(엔진의 현 개발
// 머신은 Windows). 윈도우 빌드에선 #if 로 배제돼 빈 TU 다. 안드로이드/리눅스 빌드
// 재개 시 컴파일·실소켓 검증 필요.
class CPosixTcpSocket final : public ISocket
{
public:
	CPosixTcpSocket() = default;
	explicit CPosixTcpSocket(int acceptedFd);
	~CPosixTcpSocket() override;

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
	int m_fd = -1;
};

#endif // !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB
