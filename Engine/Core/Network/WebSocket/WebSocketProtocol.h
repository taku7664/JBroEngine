#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RFC6455 WebSocket 와이어 코덱 — 플랫폼 무관 순수 코드.
// 소켓 I/O 는 모른다. 바이트 버퍼 in/out 만 다룬다(네이티브 transport 가 구동).
// 웹(브라우저)은 이 코덱을 쓰지 않는다 — 브라우저가 WS 를 네이티브로 처리하므로.
namespace WebSocket
{
	enum class EOpcode : std::uint8_t
	{
		Continuation = 0x0,
		Text         = 0x1,
		Binary       = 0x2,
		Close        = 0x8,
		Ping         = 0x9,
		Pong         = 0xA,
	};

	// 증분 파서/디코더 공통 결과.
	enum class EParse
	{
		NeedMoreData, // 버퍼에 완결 데이터 부족 — 더 받고 재시도.
		Invalid,      // 프로토콜 위반 — 연결 종료.
		Ok,           // 완결. outConsumed 만큼 소비.
	};

	// ── 서버측 오프닝 핸드셰이크 ────────────────────────────────────────────────
	struct ServerHandshakeRequest
	{
		std::string SecWebSocketKey;
		bool        WantsBinarySubprotocol = false;
	};

	// 수신 HTTP 요청 버퍼에서 WebSocket 업그레이드 요청을 파싱.
	// Ok 시 outConsumed = 요청 헤더 전체 바이트("\r\n\r\n" 포함).
	EParse ParseServerHandshake(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, ServerHandshakeRequest& outRequest);

	// 101 Switching Protocols 응답 문자열 생성.
	std::string BuildServerHandshakeResponse(const ServerHandshakeRequest& request);

	// ── 클라이언트측 오프닝 핸드셰이크 ──────────────────────────────────────────
	// GET 업그레이드 요청 생성. clientKey 는 GenerateClientKey 산출물.
	std::string BuildClientHandshakeRequest(
		const std::string& hostHeader, const std::string& path, const std::string& clientKey);

	// 서버 101 응답 검증. expectedAccept = ComputeAcceptKey(우리가 보낸 clientKey).
	// Ok 시 outConsumed = 응답 헤더 전체 바이트.
	EParse ParseClientHandshakeResponse(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, const std::string& expectedAccept);

	// ── 프레이밍 ────────────────────────────────────────────────────────────────
	struct DecodedFrame
	{
		EOpcode                   Opcode = EOpcode::Binary;
		bool                      Fin    = true;
		std::vector<std::uint8_t> Payload; // 마스크 해제 완료.
	};

	// 단일 프레임(FIN=1) 을 out 뒤에 append.
	// mask=true 면 4바이트 마스크(maskKey) 적용 — 클라이언트→서버 규약. 서버 송신은 mask=false.
	void EncodeFrame(
		std::vector<std::uint8_t>& out, EOpcode opcode,
		const void* payload, std::size_t length, bool mask, std::uint32_t maskKey);

	// 버퍼에서 완결 프레임 1개 디코드. Ok 시 outConsumed = 그 프레임 총 바이트.
	EParse DecodeFrame(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, DecodedFrame& outFrame);
}
