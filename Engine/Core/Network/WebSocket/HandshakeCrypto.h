#pragma once

#include <cstdint>
#include <string>

// WebSocket 오프닝 핸드셰이크(RFC6455)에 필요한 최소 암호 유틸.
// SHA-1 / Base64 는 accept-key 계산이라는 고정 의식(비밀 목적 아님)에만 쓰이므로
// 외부 라이브러리 없이 자체 구현한다 — 전 네이티브 플랫폼(윈/안드로) 동일 코드.
namespace WebSocket
{
	// 임의 바이트열 → Base64 문자열.
	std::string Base64Encode(const std::uint8_t* data, std::size_t size);

	// 클라이언트가 보낸 Sec-WebSocket-Key 로부터 서버 응답용 Sec-WebSocket-Accept 를 만든다.
	//   accept = Base64( SHA1( key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" ) )
	std::string ComputeAcceptKey(const std::string& clientKey);

	// 16바이트 논스를 Base64 로 인코딩해 클라이언트 Sec-WebSocket-Key 를 만든다.
	// seed 는 예측 난수원(엔진 RNG/카운터). 보안이 아니라 프레임 유일성 목적.
	std::string GenerateClientKey(std::uint64_t seed);
}
