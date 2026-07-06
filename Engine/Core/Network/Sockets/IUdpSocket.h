#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/Sockets/ISocket.h" // ESocketIo
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <cstring>

// 네이티브 UDP(비신뢰) 소켓 추상. 연결지향 ISocket 과 별개 — 데이터그램/주소 기반.
// 웹은 UDP 자체가 불가하므로 이 계층은 네이티브에서만 컴파일된다(#if 로 배제).

// 불투명 UDP 엔드포인트(sockaddr 저장). 학습한 상대 주소로 되돌려 보내는 데 쓴다.
// POD — 비교/복사 가능. 28바이트면 sockaddr_in/in6 모두 수용.
struct NetUdpEndpoint
{
	std::uint8_t  Data[28] = {};
	std::uint32_t Length   = 0;

	bool Valid() const { return 0 != Length; }
	bool Equals(const NetUdpEndpoint& other) const
	{
		return Length == other.Length && 0 == std::memcmp(Data, other.Data, Length);
	}
};

class IUdpSocket
{
public:
	virtual ~IUdpSocket() = default;

	// 소켓 생성 + 논블로킹. 바인드 없음(클라: sendto 시 ephemeral 자동 바인드).
	virtual bool Open() = 0;

	// 특정 포트 바인드(서버). port 0 이면 ephemeral.
	virtual bool Bind(std::uint16_t port) = 0;

	// host:port → 엔드포인트 해석(클라가 서버 UDP 주소 만들 때).
	virtual bool Resolve(const char* host, std::uint16_t port, NetUdpEndpoint& outEndpoint) = 0;

	// 데이터그램 송신.
	virtual ESocketIo SendTo(const NetUdpEndpoint& to, const void* data, std::size_t size) = 0;

	// 데이터그램 수신. Ok 시 outBytes(>0) + outFrom(발신 주소). 없으면 WouldBlock.
	virtual ESocketIo RecvFrom(void* buffer, std::size_t bufferSize, std::size_t& outBytes, NetUdpEndpoint& outFrom) = 0;

	virtual void Close() = 0;
	virtual bool IsValid() const = 0;
};

OwnerPtr<IUdpSocket> CreateUdpSocket();

#endif // !JBRO_PLATFORM_WEB
