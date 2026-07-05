#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>

// 네이티브 논블로킹 TCP 소켓 추상.
// Windows(WinSock2) / 추후 Android·iOS(POSIX) 가 각각 구현한다.
// WebSocket 코덱과 결합해 네이티브 WS transport 를 이룬다.
// 브라우저는 소켓 개념 자체가 없어 이 계층을 쓰지 않는다(#if 로 배제).
//
// 모든 연산은 논블로킹. I/O 결과는 명시적으로 구분한다(closed 와 would-block 을 절대 섞지 않음).

enum class ESocketIo
{
	Ok,         // 요청 처리됨(outBytes 참조).
	WouldBlock, // 지금 당장은 진행 불가 — 다음 폴에서 재시도.
	Closed,     // 피어가 정상 종료.
	Error,      // 소켓 오류 — 연결 폐기.
};

enum class ESocketConnect
{
	Pending,   // 논블로킹 connect 진행 중.
	Connected, // 연결 완료.
	Failed,    // 연결 실패.
};

class ISocket
{
public:
	virtual ~ISocket() = default;

	// 클라이언트: host:port 로 논블로킹 연결 시작. 즉시 실패 시 false.
	// 완료 여부는 PollConnect() 로 확인.
	virtual bool Connect(const char* host, std::uint16_t port) = 0;

	// 서버: 로컬 port 에서 listen 시작.
	virtual bool Listen(std::uint16_t port) = 0;

	// 서버: 대기 중 연결 하나를 수락. 없으면 nullptr(정상). 반환 소켓은 Connected 상태.
	virtual OwnerPtr<ISocket> Accept() = 0;

	// 클라이언트: 논블로킹 connect 진행 상태 폴.
	virtual ESocketConnect PollConnect() = 0;

	// 최대 size 바이트 수신. Ok 시 outBytes 에 실제 바이트 수(>0).
	virtual ESocketIo Recv(void* buffer, std::size_t size, std::size_t& outBytes) = 0;

	// 최대 size 바이트 송신. Ok 시 outBytes 에 실제 전송 바이트(부분 가능).
	virtual ESocketIo Send(const void* data, std::size_t size, std::size_t& outBytes) = 0;

	virtual void Close() = 0;
	virtual bool IsValid() const = 0;
};

// 플랫폼별 팩토리. 정의는 각 플랫폼 cpp 가 #if 가드로 하나만 제공한다.
OwnerPtr<ISocket> CreateTcpSocket();

#endif // !JBRO_PLATFORM_WEB
