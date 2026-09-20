#include <JBro/Network/Internal/WebSocketProtocol.h>

#include <cstdio>
#include <cstring>

namespace JBro::Network::WebSocket
{
    namespace
    {
        constexpr char Base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        // RFC6455 §1.3 의 매직 GUID.
        constexpr char MagicGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        std::uint32_t RotateLeft(std::uint32_t value, int bits)
        {
            return (value << bits) | (value >> (32 - bits));
        }

        void Sha1Block(const std::uint8_t block[64], std::uint32_t state[5])
        {
            std::uint32_t w[80];
            for (int index = 0; index < 16; ++index)
            {
                const std::uint8_t* at = block + index * 4;
                w[index] = (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16)
                    | (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
            }
            for (int index = 16; index < 80; ++index)
            {
                w[index] = RotateLeft(w[index - 3] ^ w[index - 8] ^ w[index - 14] ^ w[index - 16], 1);
            }
            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            for (int index = 0; index < 80; ++index)
            {
                std::uint32_t f = 0;
                std::uint32_t k = 0;
                if (index < 20)
                {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999u;
                }
                else if (index < 40)
                {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1u;
                }
                else if (index < 60)
                {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDCu;
                }
                else
                {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6u;
                }
                const std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[index];
                e = d;
                d = c;
                c = RotateLeft(b, 30);
                b = a;
                a = temp;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        char ToLower(char c)
        {
            if (c >= 'A' && c <= 'Z')
            {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c;
        }

        bool EqualsIgnoreCase(const char* a, std::uint32_t aLength, const char* b)
        {
            std::uint32_t index = 0;
            for (; index < aLength && b[index] != '\0'; ++index)
            {
                if (ToLower(a[index]) != ToLower(b[index]))
                {
                    return false;
                }
            }
            return index == aLength && b[index] == '\0';
        }

        bool ContainsIgnoreCase(const char* haystack, std::uint32_t haystackLength, const char* needle)
        {
            const std::uint32_t needleLength = static_cast<std::uint32_t>(std::strlen(needle));
            if (needleLength > haystackLength)
            {
                return false;
            }
            for (std::uint32_t start = 0; start + needleLength <= haystackLength; ++start)
            {
                bool match = true;
                for (std::uint32_t index = 0; index < needleLength; ++index)
                {
                    if (ToLower(haystack[start + index]) != ToLower(needle[index]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    return true;
                }
            }
            return false;
        }

        // "\r\n\r\n" 다음 오프셋. 없으면 0.
        std::uint32_t FindHeaderEnd(const std::uint8_t* data, std::uint32_t size)
        {
            for (std::uint32_t index = 0; index + 4 <= size; ++index)
            {
                if (data[index] == '\r' && data[index + 1] == '\n' && data[index + 2] == '\r' && data[index + 3] == '\n')
                {
                    return index + 4;
                }
            }
            return 0;
        }

        struct Span
        {
            const char* data = nullptr;
            std::uint32_t length = 0;
        };

        Span Trim(Span span)
        {
            while (span.length > 0 && IsSpace(span.data[0]))
            {
                ++span.data;
                --span.length;
            }
            while (span.length > 0 && IsSpace(span.data[span.length - 1]))
            {
                --span.length;
            }
            return span;
        }

        // 헤더 블록에서 이름이 맞는 필드의 값을 찾는다. 첫 줄은 건너뛴다.
        bool FindHeaderField(const std::uint8_t* data, std::uint32_t headerLength, const char* name, Span& outValue)
        {
            const char* text = reinterpret_cast<const char*>(data);
            std::uint32_t position = 0;
            bool firstLine = true;
            while (position < headerLength)
            {
                std::uint32_t lineEnd = position;
                while (lineEnd + 1 < headerLength && false == (text[lineEnd] == '\r' && text[lineEnd + 1] == '\n'))
                {
                    ++lineEnd;
                }
                const Span line{ text + position, lineEnd - position };
                position = lineEnd + 2;
                if (firstLine)
                {
                    firstLine = false;
                    continue;
                }
                if (0 == line.length)
                {
                    continue;
                }
                std::uint32_t colon = 0;
                while (colon < line.length && line.data[colon] != ':')
                {
                    ++colon;
                }
                if (colon >= line.length)
                {
                    continue;
                }
                const Span fieldName = Trim(Span{ line.data, colon });
                if (EqualsIgnoreCase(fieldName.data, fieldName.length, name))
                {
                    outValue = Trim(Span{ line.data + colon + 1, line.length - colon - 1 });
                    return true;
                }
            }
            return false;
        }

        Span FirstLine(const std::uint8_t* data, std::uint32_t headerLength)
        {
            const char* text = reinterpret_cast<const char*>(data);
            std::uint32_t end = 0;
            while (end + 1 < headerLength && false == (text[end] == '\r' && text[end + 1] == '\n'))
            {
                ++end;
            }
            return Span{ text, end };
        }

        bool Append(char* out, std::uint32_t capacity, std::uint32_t& length, const char* text)
        {
            const std::uint32_t textLength = static_cast<std::uint32_t>(std::strlen(text));
            if (length + textLength >= capacity)
            {
                return false;
            }
            std::memcpy(out + length, text, textLength);
            length += textLength;
            out[length] = '\0';
            return true;
        }
    }

    // ── 핸드셰이크 암호 ─────────────────────────────────────────────────────────────────────────

    void Sha1(const std::uint8_t* message, std::size_t length, std::uint8_t outDigest[20])
    {
        std::uint32_t state[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
        std::size_t offset = 0;
        while (length - offset >= 64)
        {
            Sha1Block(message + offset, state);
            offset += 64;
        }
        // 남은 것 + 0x80 + 0 채움 + 64 비트 길이(빅엔디언). 두 블록까지 필요할 수 있다.
        std::uint8_t tail[128] = {};
        const std::size_t remaining = length - offset;
        std::memcpy(tail, message + offset, remaining);
        tail[remaining] = 0x80;
        const std::size_t tailBlocks = (remaining + 1 + 8 <= 64) ? 1 : 2;
        const std::uint64_t bitLength = static_cast<std::uint64_t>(length) * 8u;
        for (int index = 0; index < 8; ++index)
        {
            tail[tailBlocks * 64 - 1 - index] = static_cast<std::uint8_t>((bitLength >> (index * 8)) & 0xFFu);
        }
        Sha1Block(tail, state);
        if (2 == tailBlocks)
        {
            Sha1Block(tail + 64, state);
        }
        for (int index = 0; index < 5; ++index)
        {
            outDigest[index * 4 + 0] = static_cast<std::uint8_t>((state[index] >> 24) & 0xFFu);
            outDigest[index * 4 + 1] = static_cast<std::uint8_t>((state[index] >> 16) & 0xFFu);
            outDigest[index * 4 + 2] = static_cast<std::uint8_t>((state[index] >> 8) & 0xFFu);
            outDigest[index * 4 + 3] = static_cast<std::uint8_t>(state[index] & 0xFFu);
        }
    }

    std::uint32_t Base64Encode(const std::uint8_t* data, std::size_t size, char* out, std::uint32_t capacity)
    {
        const std::uint32_t needed = static_cast<std::uint32_t>(((size + 2u) / 3u) * 4u);
        if (needed > capacity)
        {
            return 0;
        }
        std::uint32_t written = 0;
        std::size_t index = 0;
        while (index + 3u <= size)
        {
            const std::uint32_t triple = (static_cast<std::uint32_t>(data[index]) << 16)
                | (static_cast<std::uint32_t>(data[index + 1]) << 8) | static_cast<std::uint32_t>(data[index + 2]);
            out[written++] = Base64Alphabet[(triple >> 18) & 0x3Fu];
            out[written++] = Base64Alphabet[(triple >> 12) & 0x3Fu];
            out[written++] = Base64Alphabet[(triple >> 6) & 0x3Fu];
            out[written++] = Base64Alphabet[triple & 0x3Fu];
            index += 3u;
        }
        const std::size_t remaining = size - index;
        if (1u == remaining)
        {
            const std::uint32_t triple = static_cast<std::uint32_t>(data[index]) << 16;
            out[written++] = Base64Alphabet[(triple >> 18) & 0x3Fu];
            out[written++] = Base64Alphabet[(triple >> 12) & 0x3Fu];
            out[written++] = '=';
            out[written++] = '=';
        }
        else if (2u == remaining)
        {
            const std::uint32_t triple = (static_cast<std::uint32_t>(data[index]) << 16)
                | (static_cast<std::uint32_t>(data[index + 1]) << 8);
            out[written++] = Base64Alphabet[(triple >> 18) & 0x3Fu];
            out[written++] = Base64Alphabet[(triple >> 12) & 0x3Fu];
            out[written++] = Base64Alphabet[(triple >> 6) & 0x3Fu];
            out[written++] = '=';
        }
        return written;
    }

    void ComputeAcceptKey(const char* clientKey, std::uint32_t clientKeyLength, char* out)
    {
        std::uint8_t combined[ServerHandshakeRequest::KeyCapacity + sizeof(MagicGuid)];
        const std::uint32_t keyLength = clientKeyLength < ServerHandshakeRequest::KeyCapacity
            ? clientKeyLength
            : ServerHandshakeRequest::KeyCapacity;
        std::memcpy(combined, clientKey, keyLength);
        std::memcpy(combined + keyLength, MagicGuid, sizeof(MagicGuid) - 1);
        std::uint8_t digest[20];
        Sha1(combined, keyLength + sizeof(MagicGuid) - 1, digest);
        const std::uint32_t written = Base64Encode(digest, sizeof(digest), out, AcceptKeyChars);
        out[written] = '\0';
    }

    void GenerateClientKey(std::uint64_t seed, char* out)
    {
        std::uint64_t state = (0 != seed) ? seed : 0x9E3779B97F4A7C15ull;
        std::uint8_t nonce[16];
        for (int index = 0; index < 16; index += 8)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            for (int byte = 0; byte < 8; ++byte)
            {
                nonce[index + byte] = static_cast<std::uint8_t>((state >> (byte * 8)) & 0xFFu);
            }
        }
        const std::uint32_t written = Base64Encode(nonce, sizeof(nonce), out, ClientKeyChars);
        out[written] = '\0';
    }

    // ── 오프닝 핸드셰이크 ───────────────────────────────────────────────────────────────────────

    ParseResult ParseServerHandshake(
        const std::uint8_t* data, std::uint32_t size, std::uint32_t& outConsumed, ServerHandshakeRequest& outRequest)
    {
        const std::uint32_t headerEnd = FindHeaderEnd(data, size);
        if (0 == headerEnd)
        {
            return ParseResult::NeedMoreData;
        }
        const Span first = FirstLine(data, headerEnd);
        if (first.length < 4 || 0 != std::memcmp(first.data, "GET ", 4))
        {
            return ParseResult::Invalid;
        }
        Span upgrade;
        Span key;
        if (false == FindHeaderField(data, headerEnd, "Upgrade", upgrade)
            || false == ContainsIgnoreCase(upgrade.data, upgrade.length, "websocket")
            || false == FindHeaderField(data, headerEnd, "Sec-WebSocket-Key", key)
            || key.length >= ServerHandshakeRequest::KeyCapacity || 0 == key.length)
        {
            return ParseResult::Invalid;
        }
        std::memcpy(outRequest.key, key.data, key.length);
        outRequest.key[key.length] = '\0';
        outRequest.keyLength = key.length;
        Span protocols;
        outRequest.wantsBinarySubprotocol = FindHeaderField(data, headerEnd, "Sec-WebSocket-Protocol", protocols)
            && ContainsIgnoreCase(protocols.data, protocols.length, "binary");
        outConsumed = headerEnd;
        return ParseResult::Ok;
    }

    std::uint32_t BuildServerHandshakeResponse(const ServerHandshakeRequest& request, char* out, std::uint32_t capacity)
    {
        char accept[AcceptKeyChars + 1];
        ComputeAcceptKey(request.key, request.keyLength, accept);
        std::uint32_t length = 0;
        bool ok = Append(out, capacity, length, "HTTP/1.1 101 Switching Protocols\r\n")
            && Append(out, capacity, length, "Upgrade: websocket\r\n")
            && Append(out, capacity, length, "Connection: Upgrade\r\n")
            && Append(out, capacity, length, "Sec-WebSocket-Accept: ")
            && Append(out, capacity, length, accept)
            && Append(out, capacity, length, "\r\n");
        if (ok && request.wantsBinarySubprotocol)
        {
            ok = Append(out, capacity, length, "Sec-WebSocket-Protocol: binary\r\n");
        }
        if (ok)
        {
            ok = Append(out, capacity, length, "\r\n");
        }
        return ok ? length : 0;
    }

    std::uint32_t BuildClientHandshakeRequest(
        const char* host, std::uint16_t port, const char* clientKey, char* out, std::uint32_t capacity)
    {
        char portText[8];
        std::snprintf(portText, sizeof(portText), "%u", static_cast<unsigned>(port));
        std::uint32_t length = 0;
        const bool ok = Append(out, capacity, length, "GET / HTTP/1.1\r\n")
            && Append(out, capacity, length, "Host: ")
            && Append(out, capacity, length, host)
            && Append(out, capacity, length, ":")
            && Append(out, capacity, length, portText)
            && Append(out, capacity, length, "\r\n")
            && Append(out, capacity, length, "Upgrade: websocket\r\n")
            && Append(out, capacity, length, "Connection: Upgrade\r\n")
            && Append(out, capacity, length, "Sec-WebSocket-Key: ")
            && Append(out, capacity, length, clientKey)
            && Append(out, capacity, length, "\r\n")
            && Append(out, capacity, length, "Sec-WebSocket-Version: 13\r\n")
            && Append(out, capacity, length, "Sec-WebSocket-Protocol: binary\r\n")
            && Append(out, capacity, length, "\r\n");
        return ok ? length : 0;
    }

    ParseResult ParseClientHandshakeResponse(
        const std::uint8_t* data, std::uint32_t size, std::uint32_t& outConsumed, const char* expectedAccept)
    {
        const std::uint32_t headerEnd = FindHeaderEnd(data, size);
        if (0 == headerEnd)
        {
            return ParseResult::NeedMoreData;
        }
        const Span first = FirstLine(data, headerEnd);
        if (false == ContainsIgnoreCase(first.data, first.length, " 101"))
        {
            return ParseResult::Invalid;
        }
        Span accept;
        if (false == FindHeaderField(data, headerEnd, "Sec-WebSocket-Accept", accept))
        {
            return ParseResult::Invalid;
        }
        if (accept.length != std::strlen(expectedAccept) || 0 != std::memcmp(accept.data, expectedAccept, accept.length))
        {
            return ParseResult::Invalid;
        }
        outConsumed = headerEnd;
        return ParseResult::Ok;
    }

    // ── 프레이밍 ────────────────────────────────────────────────────────────────────────────────

    std::uint32_t EncodeFrameHeader(Opcode opcode, bool fin, std::uint64_t payloadLength, bool mask, std::uint32_t maskKey,
        std::uint8_t* out)
    {
        std::uint32_t length = 0;
        out[length++] = static_cast<std::uint8_t>((fin ? 0x80u : 0x00u) | static_cast<std::uint8_t>(opcode));
        const std::uint8_t maskBit = mask ? 0x80u : 0x00u;
        if (payloadLength <= 125u)
        {
            out[length++] = static_cast<std::uint8_t>(maskBit | static_cast<std::uint8_t>(payloadLength));
        }
        else if (payloadLength <= 0xFFFFu)
        {
            out[length++] = static_cast<std::uint8_t>(maskBit | 126u);
            out[length++] = static_cast<std::uint8_t>((payloadLength >> 8) & 0xFFu);
            out[length++] = static_cast<std::uint8_t>(payloadLength & 0xFFu);
        }
        else
        {
            out[length++] = static_cast<std::uint8_t>(maskBit | 127u);
            for (int index = 7; index >= 0; --index)
            {
                out[length++] = static_cast<std::uint8_t>((payloadLength >> (index * 8)) & 0xFFu);
            }
        }
        if (mask)
        {
            out[length++] = static_cast<std::uint8_t>((maskKey >> 24) & 0xFFu);
            out[length++] = static_cast<std::uint8_t>((maskKey >> 16) & 0xFFu);
            out[length++] = static_cast<std::uint8_t>((maskKey >> 8) & 0xFFu);
            out[length++] = static_cast<std::uint8_t>(maskKey & 0xFFu);
        }
        return length;
    }

    ParseResult DecodeFrameHeader(const std::uint8_t* data, std::uint32_t size, FrameHeader& outHeader)
    {
        if (size < 2u)
        {
            return ParseResult::NeedMoreData;
        }
        const std::uint8_t byte0 = data[0];
        const std::uint8_t byte1 = data[1];
        if (0 != (byte0 & 0x70u))
        {
            // 확장을 협상하지 않았으니 RSV 는 0 이어야 한다.
            return ParseResult::Invalid;
        }
        outHeader.fin = 0 != (byte0 & 0x80u);
        outHeader.opcode = static_cast<Opcode>(byte0 & 0x0Fu);
        outHeader.masked = 0 != (byte1 & 0x80u);
        std::uint64_t payloadLength = byte1 & 0x7Fu;
        std::uint32_t offset = 2;
        if (126u == payloadLength)
        {
            if (size < offset + 2u)
            {
                return ParseResult::NeedMoreData;
            }
            payloadLength = (static_cast<std::uint64_t>(data[offset]) << 8) | static_cast<std::uint64_t>(data[offset + 1]);
            offset += 2;
        }
        else if (127u == payloadLength)
        {
            if (size < offset + 8u)
            {
                return ParseResult::NeedMoreData;
            }
            payloadLength = 0;
            for (int index = 0; index < 8; ++index)
            {
                payloadLength = (payloadLength << 8) | static_cast<std::uint64_t>(data[offset + index]);
            }
            offset += 8;
        }
        if (outHeader.masked)
        {
            if (size < offset + 4u)
            {
                return ParseResult::NeedMoreData;
            }
            std::memcpy(outHeader.mask, data + offset, 4);
            offset += 4;
        }
        else
        {
            std::memset(outHeader.mask, 0, 4);
        }
        outHeader.payloadLength = payloadLength;
        outHeader.headerLength = offset;
        return ParseResult::Ok;
    }

    void ApplyMask(std::uint8_t* data, std::uint32_t size, const std::uint8_t mask[4], std::uint32_t startOffset)
    {
        for (std::uint32_t index = 0; index < size; ++index)
        {
            data[index] ^= mask[(startOffset + index) & 3u];
        }
    }
}
