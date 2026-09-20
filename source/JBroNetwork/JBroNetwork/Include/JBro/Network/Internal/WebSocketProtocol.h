#pragma once

#include <cstddef>
#include <cstdint>

// RFC6455 WebSocket 와이어 코덱. 플랫폼도 소켓도 모르는 순수 코드다 - 바이트 버퍼만 다룬다.
// 기존 엔진 `WebSocketProtocol` 과 `HandshakeCrypto` 를 std::string·std::vector 없이 고정 버퍼로 옮겼다.
// 브라우저는 이 코덱을 쓰지 않는다 - 브라우저가 WS 를 스스로 말한다. 네이티브가 브라우저와 같은 와이어를 말하기 위한 것이다.
namespace JBro::Network::WebSocket
{
    enum class Opcode : std::uint8_t
    {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };

    enum class ParseResult : std::uint8_t
    {
        // 완결된 데이터가 부족하다. 더 받고 다시 시도한다.
        NeedMoreData,
        // 프로토콜 위반. 연결을 끊는다.
        Invalid,
        Ok
    };

    // Sec-WebSocket-Key 는 16 바이트 논스의 Base64(24 글자), Accept 는 SHA-1 의 Base64(28 글자)다.
    inline constexpr std::uint32_t ClientKeyChars = 24;
    inline constexpr std::uint32_t AcceptKeyChars = 28;
    // 프레임 헤더는 최대 2 + 8 + 4 바이트다.
    inline constexpr std::uint32_t MaxFrameHeaderBytes = 14;
    // 제어 프레임 페이로드 상한(RFC6455 §5.5).
    inline constexpr std::uint32_t MaxControlPayloadBytes = 125;

    // ── 핸드셰이크 암호 ─────────────────────────────────────────────────────────────────────────

    void Sha1(const std::uint8_t* message, std::size_t length, std::uint8_t outDigest[20]);
    // 채운 글자 수를 돌려준다. 널 종료는 하지 않는다. 용량이 모자라면 0 이다.
    std::uint32_t Base64Encode(const std::uint8_t* data, std::size_t size, char* out, std::uint32_t capacity);
    // `out` 은 AcceptKeyChars + 1 이상이어야 하고 널로 끝난다.
    void ComputeAcceptKey(const char* clientKey, std::uint32_t clientKeyLength, char* out);
    // `out` 은 ClientKeyChars + 1 이상이어야 하고 널로 끝난다. seed 는 유일성용이고 보안이 아니다.
    void GenerateClientKey(std::uint64_t seed, char* out);

    // ── 오프닝 핸드셰이크 ───────────────────────────────────────────────────────────────────────

    struct ServerHandshakeRequest
    {
        static constexpr std::uint32_t KeyCapacity = 64;
        char key[KeyCapacity] = {};
        std::uint32_t keyLength = 0;
        bool wantsBinarySubprotocol = false;
    };

    // 수신 HTTP 요청에서 업그레이드 요청을 읽는다. Ok 면 `outConsumed` 는 헤더 블록 전체("\r\n\r\n" 포함)다.
    ParseResult ParseServerHandshake(
        const std::uint8_t* data, std::uint32_t size, std::uint32_t& outConsumed, ServerHandshakeRequest& outRequest);
    // 101 응답을 `out` 에 쓴다. 쓴 글자 수를 돌려주고 용량이 모자라면 0 이다.
    std::uint32_t BuildServerHandshakeResponse(const ServerHandshakeRequest& request, char* out, std::uint32_t capacity);
    // GET 업그레이드 요청을 `out` 에 쓴다.
    std::uint32_t BuildClientHandshakeRequest(
        const char* host, std::uint16_t port, const char* clientKey, char* out, std::uint32_t capacity);
    // 서버 101 응답을 검증한다. `expectedAccept` 는 우리가 보낸 키로 계산한 Accept 다.
    ParseResult ParseClientHandshakeResponse(
        const std::uint8_t* data, std::uint32_t size, std::uint32_t& outConsumed, const char* expectedAccept);

    // ── 프레이밍 (RFC6455 §5) ───────────────────────────────────────────────────────────────────

    struct FrameHeader
    {
        Opcode opcode = Opcode::Binary;
        bool fin = true;
        bool masked = false;
        std::uint8_t mask[4] = {};
        std::uint64_t payloadLength = 0;
        std::uint32_t headerLength = 0;
    };

    // 헤더만 쓴다. 페이로드는 호출측이 뒤에 잇는다(마스크가 켜져 있으면 `ApplyMask` 로). 쓴 바이트 수를 돌려준다.
    std::uint32_t EncodeFrameHeader(Opcode opcode, bool fin, std::uint64_t payloadLength, bool mask, std::uint32_t maskKey,
        std::uint8_t* out);
    ParseResult DecodeFrameHeader(const std::uint8_t* data, std::uint32_t size, FrameHeader& outHeader);
    // `startOffset` 은 이 구간이 페이로드의 몇 번째 바이트부터인가다 - 여러 구간으로 나눠 마스크할 때 이어 간다.
    void ApplyMask(std::uint8_t* data, std::uint32_t size, const std::uint8_t mask[4], std::uint32_t startOffset);
}
