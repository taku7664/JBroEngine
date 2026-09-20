#include "TestCheck.h"

#include <JBro/Network/Internal/WebSocketProtocol.h>

#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // **SHA-1 과 Base64 는 알려진 답과 같다.** RFC 3174 의 "abc", RFC 4648 의 "Man".
    void TestKnownDigests()
    {
        std::uint8_t digest[20];
        WebSocket::Sha1(reinterpret_cast<const std::uint8_t*>("abc"), 3, digest);
        const std::uint8_t expected[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50,
            0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
        Check(0 == std::memcmp(digest, expected, 20), "sha1('abc') matches RFC 3174");

        // 패딩 경계: 55 바이트는 꼬리 한 블록, 56 부터는 두 블록, 64 는 본문 한 블록 + 꼬리, 100 은 둘 다 걸친다.
        // 기대값은 Python hashlib 로 구했다.
        struct Vector
        {
            std::size_t length;
            std::uint8_t digest[20];
        };
        const Vector vectors[] = {
            { 55, { 0xc1, 0xc8, 0xbb, 0xdc, 0x22, 0x79, 0x6e, 0x28, 0xc0, 0xe1, 0x51, 0x63, 0xd2, 0x08, 0x99, 0xb6, 0x56, 0x21, 0xd6, 0x5a } },
            { 56, { 0xc2, 0xdb, 0x33, 0x0f, 0x60, 0x83, 0x85, 0x4c, 0x99, 0xd4, 0xb5, 0xbf, 0xb6, 0xe8, 0xf2, 0x9f, 0x20, 0x1b, 0xe6, 0x99 } },
            { 63, { 0x03, 0xf0, 0x9f, 0x5b, 0x15, 0x8a, 0x7a, 0x8c, 0xda, 0xd9, 0x20, 0xbd, 0xdc, 0x29, 0xb8, 0x1c, 0x18, 0xa5, 0x51, 0xf5 } },
            { 64, { 0x00, 0x98, 0xba, 0x82, 0x4b, 0x5c, 0x16, 0x42, 0x7b, 0xd7, 0xa1, 0x12, 0x2a, 0x5a, 0x44, 0x2a, 0x25, 0xec, 0x64, 0x4d } },
            { 100, { 0x7f, 0x90, 0x00, 0x25, 0x7a, 0x49, 0x18, 0xd7, 0x07, 0x26, 0x55, 0xea, 0x46, 0x85, 0x40, 0xcd, 0xcb, 0xd4, 0x2e, 0x0c } },
        };
        std::uint8_t longMessage[100];
        std::memset(longMessage, 'a', sizeof(longMessage));
        for (const Vector& vector : vectors)
        {
            WebSocket::Sha1(longMessage, vector.length, digest);
            Check(0 == std::memcmp(digest, vector.digest, 20), "sha1 of repeated 'a' matches hashlib at every padding boundary");
        }

        char text[16];
        const std::uint32_t written = WebSocket::Base64Encode(reinterpret_cast<const std::uint8_t*>("Man"), 3, text, sizeof(text));
        Check(written == 4 && 0 == std::memcmp(text, "TWFu", 4), "base64('Man') is 'TWFu'");
        const std::uint32_t padded = WebSocket::Base64Encode(reinterpret_cast<const std::uint8_t*>("Ma"), 2, text, sizeof(text));
        Check(padded == 4 && 0 == std::memcmp(text, "TWE=", 4), "base64('Ma') pads once");
        Check(0 == WebSocket::Base64Encode(reinterpret_cast<const std::uint8_t*>("Man"), 3, text, 3), "a short buffer is refused");
    }

    // **Accept 키는 RFC 6455 §1.3 의 예제와 같다.** 브라우저가 이 값을 검사한다.
    void TestAcceptKeyMatchesRfcExample()
    {
        const char* key = "dGhlIHNhbXBsZSBub25jZQ==";
        char accept[WebSocket::AcceptKeyChars + 1];
        WebSocket::ComputeAcceptKey(key, static_cast<std::uint32_t>(std::strlen(key)), accept);
        Check(0 == std::strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), "the accept key is the RFC example");

        char clientKey[WebSocket::ClientKeyChars + 1];
        WebSocket::GenerateClientKey(42, clientKey);
        Check(std::strlen(clientKey) == WebSocket::ClientKeyChars, "a client key is 24 base64 chars");
        Check(clientKey[22] == '=' && clientKey[23] == '=', "16 bytes pad twice");
        char another[WebSocket::ClientKeyChars + 1];
        WebSocket::GenerateClientKey(43, another);
        Check(0 != std::strcmp(clientKey, another), "different seeds give different keys");
    }

    // **핸드셰이크는 요청을 읽고 응답을 만들고, 그 응답을 클라이언트가 검증한다.**
    void TestHandshakeRoundTrip()
    {
        char request[512];
        const std::uint32_t requestLength = WebSocket::BuildClientHandshakeRequest(
            "example.com", 7777, "dGhlIHNhbXBsZSBub25jZQ==", request, sizeof(request));
        Check(requestLength > 0, "the request is built");
        Check(nullptr != std::strstr(request, "Host: example.com:7777\r\n"), "with the host header");
        Check(nullptr != std::strstr(request, "Sec-WebSocket-Protocol: binary\r\n"), "asking for the binary subprotocol");

        // 절반만 왔을 때는 더 달라고 한다.
        std::uint32_t consumed = 0;
        WebSocket::ServerHandshakeRequest parsed;
        Check(WebSocket::ParseServerHandshake(reinterpret_cast<const std::uint8_t*>(request), requestLength / 2, consumed, parsed)
                == WebSocket::ParseResult::NeedMoreData,
            "half a request needs more data");
        Check(WebSocket::ParseServerHandshake(reinterpret_cast<const std::uint8_t*>(request), requestLength, consumed, parsed)
                == WebSocket::ParseResult::Ok,
            "the full request parses");
        Check(consumed == requestLength, "and consumes exactly the header block");
        Check(0 == std::strcmp(parsed.key, "dGhlIHNhbXBsZSBub25jZQ=="), "with the key");
        Check(parsed.wantsBinarySubprotocol, "and the subprotocol wish");

        char response[512];
        const std::uint32_t responseLength = WebSocket::BuildServerHandshakeResponse(parsed, response, sizeof(response));
        Check(responseLength > 0, "the response is built");
        Check(nullptr != std::strstr(response, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), "with the accept key");
        Check(WebSocket::ParseClientHandshakeResponse(reinterpret_cast<const std::uint8_t*>(response), responseLength, consumed,
                  "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
                == WebSocket::ParseResult::Ok,
            "the client accepts its own expected key");
        Check(WebSocket::ParseClientHandshakeResponse(reinterpret_cast<const std::uint8_t*>(response), responseLength, consumed,
                  "AAAAAAAAAAAAAAAAAAAAAAAAAAA=")
                == WebSocket::ParseResult::Invalid,
            "and rejects a wrong one");

        const char* notUpgrade = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
        Check(WebSocket::ParseServerHandshake(reinterpret_cast<const std::uint8_t*>(notUpgrade),
                  static_cast<std::uint32_t>(std::strlen(notUpgrade)), consumed, parsed)
                == WebSocket::ParseResult::Invalid,
            "a plain GET is not a websocket upgrade");
    }

    void CheckHeaderRoundTrip(std::uint64_t length, bool mask, std::uint32_t expectedHeaderLength)
    {
        std::uint8_t header[WebSocket::MaxFrameHeaderBytes];
        const std::uint32_t written = WebSocket::EncodeFrameHeader(WebSocket::Opcode::Binary, true, length, mask, 0xA1B2C3D4u, header);
        Check(written == expectedHeaderLength, "the header length matches the payload length class");
        WebSocket::FrameHeader decoded;
        Check(WebSocket::DecodeFrameHeader(header, written, decoded) == WebSocket::ParseResult::Ok, "and decodes");
        Check(decoded.payloadLength == length, "to the same length");
        Check(decoded.headerLength == written, "and the same header size");
        Check(decoded.masked == mask, "and the same mask bit");
        Check(decoded.fin && decoded.opcode == WebSocket::Opcode::Binary, "fin and opcode survive");
        if (mask)
        {
            Check(decoded.mask[0] == 0xA1 && decoded.mask[3] == 0xD4, "the mask bytes are big-endian key bytes");
        }
        Check(WebSocket::DecodeFrameHeader(header, written - 1, decoded) == WebSocket::ParseResult::NeedMoreData,
            "one byte short asks for more");
    }

    // **프레임 헤더는 세 가지 길이 형식과 마스크 유무 전부에서 왕복한다.**
    void TestFrameHeaderRoundTrip()
    {
        CheckHeaderRoundTrip(5, false, 2);
        CheckHeaderRoundTrip(125, true, 6);
        CheckHeaderRoundTrip(126, false, 4);
        CheckHeaderRoundTrip(65535, true, 8);
        CheckHeaderRoundTrip(65536, false, 10);
        CheckHeaderRoundTrip(70000, true, 14);

        std::uint8_t reserved[2] = { 0x80 | 0x40 | 0x02, 0x00 };
        WebSocket::FrameHeader decoded;
        Check(WebSocket::DecodeFrameHeader(reserved, 2, decoded) == WebSocket::ParseResult::Invalid,
            "a set RSV bit is a protocol violation");
    }

    // **마스크는 구간을 나눠 적용해도 한 번에 적용한 것과 같다.** 송신이 스크래치 크기로 잘라 마스크하기 때문이다.
    void TestMaskContinuesAcrossRuns()
    {
        const std::uint8_t mask[4] = { 1, 2, 3, 4 };
        std::uint8_t whole[10];
        std::uint8_t split[10];
        for (std::uint8_t index = 0; index < 10; ++index)
        {
            whole[index] = static_cast<std::uint8_t>(index * 11);
            split[index] = whole[index];
        }
        WebSocket::ApplyMask(whole, 10, mask, 0);
        WebSocket::ApplyMask(split, 3, mask, 0);
        WebSocket::ApplyMask(split + 3, 7, mask, 3);
        Check(0 == std::memcmp(whole, split, 10), "split masking equals whole masking");
        WebSocket::ApplyMask(whole, 10, mask, 0);
        Check(whole[1] == 11, "masking twice restores the bytes");
    }
}

int RunWebSocketProtocolTests()
{
    try
    {
        TestKnownDigests();
        TestAcceptKeyMatchesRfcExample();
        TestHandshakeRoundTrip();
        TestFrameHeaderRoundTrip();
        TestMaskContinuesAcrossRuns();
    }
    catch (const std::exception& error)
    {
        std::cout << "WebSocketProtocolTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "WebSocketProtocolTests passed\n";
    return 0;
}
