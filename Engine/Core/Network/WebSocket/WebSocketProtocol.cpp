#include "pch.h"
#include "WebSocketProtocol.h"

#include "HandshakeCrypto.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace WebSocket
{
	namespace
	{
		// 대소문자 무시 비교(ASCII).
		bool EqualsIgnoreCase(const std::string& a, const char* b)
		{
			std::size_t i = 0;
			for (; i < a.size() && b[i] != '\0'; ++i)
			{
				const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
				const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
				if (ca != cb)
				{
					return false;
				}
			}
			return i == a.size() && b[i] == '\0';
		}

		bool ContainsIgnoreCase(const std::string& haystack, const char* needle)
		{
			std::string lowerHay = haystack;
			std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::string lowerNeedle = needle;
			std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lowerHay.find(lowerNeedle) != std::string::npos;
		}

		std::string Trim(const std::string& s)
		{
			std::size_t start = 0;
			std::size_t end   = s.size();
			while (start < end && std::isspace(static_cast<unsigned char>(s[start])))
			{
				++start;
			}
			while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
			{
				--end;
			}
			return s.substr(start, end - start);
		}

		// 헤더 블록 끝("\r\n\r\n")을 찾는다. 없으면 npos.
		std::size_t FindHeaderEnd(const std::uint8_t* data, std::size_t size)
		{
			if (size < 4)
			{
				return std::string::npos;
			}
			for (std::size_t i = 0; i + 4 <= size; ++i)
			{
				if (data[i] == '\r' && data[i + 1] == '\n' &&
				    data[i + 2] == '\r' && data[i + 3] == '\n')
				{
					return i + 4; // 헤더 종료 다음 오프셋.
				}
			}
			return std::string::npos;
		}

		// 헤더 블록을 (첫 줄, name→value 맵 유사) 로 파싱. name 은 소문자 정규화.
		struct ParsedHeaders
		{
			std::string FirstLine;
			std::vector<std::pair<std::string, std::string>> Fields;

			const std::string* Find(const char* name) const
			{
				for (const auto& kv : Fields)
				{
					if (EqualsIgnoreCase(kv.first, name))
					{
						return &kv.second;
					}
				}
				return nullptr;
			}
		};

		ParsedHeaders ParseHeaderBlock(const std::uint8_t* data, std::size_t headerLen)
		{
			ParsedHeaders result;
			std::string block(reinterpret_cast<const char*>(data), headerLen);

			std::size_t pos      = 0;
			bool        firstDone = false;
			while (pos < block.size())
			{
				std::size_t lineEnd = block.find("\r\n", pos);
				if (lineEnd == std::string::npos)
				{
					lineEnd = block.size();
				}
				std::string line = block.substr(pos, lineEnd - pos);
				pos = lineEnd + 2;

				if (line.empty())
				{
					continue;
				}
				if (!firstDone)
				{
					result.FirstLine = line;
					firstDone        = true;
					continue;
				}

				const std::size_t colon = line.find(':');
				if (colon == std::string::npos)
				{
					continue;
				}
				std::string name  = Trim(line.substr(0, colon));
				std::string value = Trim(line.substr(colon + 1));
				result.Fields.emplace_back(std::move(name), std::move(value));
			}
			return result;
		}
	}

	// ── 서버측 핸드셰이크 ────────────────────────────────────────────────────────

	EParse ParseServerHandshake(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, ServerHandshakeRequest& outRequest)
	{
		const std::size_t headerEnd = FindHeaderEnd(data, size);
		if (headerEnd == std::string::npos)
		{
			return EParse::NeedMoreData;
		}

		const ParsedHeaders headers = ParseHeaderBlock(data, headerEnd);

		// 요청 라인은 "GET <path> HTTP/1.1".
		if (headers.FirstLine.rfind("GET ", 0) != 0)
		{
			return EParse::Invalid;
		}

		const std::string* upgrade = headers.Find("Upgrade");
		const std::string* key     = headers.Find("Sec-WebSocket-Key");
		if (nullptr == upgrade || !ContainsIgnoreCase(*upgrade, "websocket") || nullptr == key)
		{
			return EParse::Invalid;
		}

		outRequest.SecWebSocketKey = *key;

		const std::string* protocols = headers.Find("Sec-WebSocket-Protocol");
		outRequest.WantsBinarySubprotocol =
			(nullptr != protocols) && ContainsIgnoreCase(*protocols, "binary");

		outConsumed = headerEnd;
		return EParse::Ok;
	}

	std::string BuildServerHandshakeResponse(const ServerHandshakeRequest& request)
	{
		std::string response;
		response += "HTTP/1.1 101 Switching Protocols\r\n";
		response += "Upgrade: websocket\r\n";
		response += "Connection: Upgrade\r\n";
		response += "Sec-WebSocket-Accept: ";
		response += ComputeAcceptKey(request.SecWebSocketKey);
		response += "\r\n";
		if (request.WantsBinarySubprotocol)
		{
			response += "Sec-WebSocket-Protocol: binary\r\n";
		}
		response += "\r\n";
		return response;
	}

	// ── 클라이언트측 핸드셰이크 ──────────────────────────────────────────────────

	std::string BuildClientHandshakeRequest(
		const std::string& hostHeader, const std::string& path, const std::string& clientKey)
	{
		const std::string& safePath = path.empty() ? std::string("/") : path;

		std::string request;
		request += "GET ";
		request += safePath;
		request += " HTTP/1.1\r\n";
		request += "Host: ";
		request += hostHeader;
		request += "\r\n";
		request += "Upgrade: websocket\r\n";
		request += "Connection: Upgrade\r\n";
		request += "Sec-WebSocket-Key: ";
		request += clientKey;
		request += "\r\n";
		request += "Sec-WebSocket-Version: 13\r\n";
		request += "Sec-WebSocket-Protocol: binary\r\n";
		request += "\r\n";
		return request;
	}

	EParse ParseClientHandshakeResponse(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, const std::string& expectedAccept)
	{
		const std::size_t headerEnd = FindHeaderEnd(data, size);
		if (headerEnd == std::string::npos)
		{
			return EParse::NeedMoreData;
		}

		const ParsedHeaders headers = ParseHeaderBlock(data, headerEnd);

		// 상태 라인 "HTTP/1.1 101 ...".
		if (headers.FirstLine.find(" 101") == std::string::npos)
		{
			return EParse::Invalid;
		}

		const std::string* accept = headers.Find("Sec-WebSocket-Accept");
		if (nullptr == accept || Trim(*accept) != expectedAccept)
		{
			return EParse::Invalid;
		}

		outConsumed = headerEnd;
		return EParse::Ok;
	}

	// ── 프레이밍 (RFC6455 §5) ────────────────────────────────────────────────────

	void EncodeFrame(
		std::vector<std::uint8_t>& out, EOpcode opcode,
		const void* payload, std::size_t length, bool mask, std::uint32_t maskKey)
	{
		// byte0: FIN(1) + RSV(0) + opcode.
		out.push_back(static_cast<std::uint8_t>(0x80u | static_cast<std::uint8_t>(opcode)));

		const std::uint8_t maskBit = mask ? 0x80u : 0x00u;
		if (length <= 125u)
		{
			out.push_back(static_cast<std::uint8_t>(maskBit | static_cast<std::uint8_t>(length)));
		}
		else if (length <= 0xFFFFu)
		{
			out.push_back(static_cast<std::uint8_t>(maskBit | 126u));
			out.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFu));
			out.push_back(static_cast<std::uint8_t>((length) & 0xFFu));
		}
		else
		{
			out.push_back(static_cast<std::uint8_t>(maskBit | 127u));
			const std::uint64_t len64 = static_cast<std::uint64_t>(length);
			for (int i = 7; i >= 0; --i)
			{
				out.push_back(static_cast<std::uint8_t>((len64 >> (i * 8)) & 0xFFu));
			}
		}

		const std::uint8_t* src = static_cast<const std::uint8_t*>(payload);
		if (mask)
		{
			std::uint8_t maskBytes[4];
			maskBytes[0] = static_cast<std::uint8_t>((maskKey >> 24) & 0xFFu);
			maskBytes[1] = static_cast<std::uint8_t>((maskKey >> 16) & 0xFFu);
			maskBytes[2] = static_cast<std::uint8_t>((maskKey >> 8) & 0xFFu);
			maskBytes[3] = static_cast<std::uint8_t>((maskKey) & 0xFFu);
			out.insert(out.end(), maskBytes, maskBytes + 4);

			for (std::size_t i = 0; i < length; ++i)
			{
				out.push_back(static_cast<std::uint8_t>(src[i] ^ maskBytes[i & 3u]));
			}
		}
		else if (length > 0u)
		{
			out.insert(out.end(), src, src + length);
		}
	}

	EParse DecodeFrame(
		const std::uint8_t* data, std::size_t size,
		std::size_t& outConsumed, DecodedFrame& outFrame)
	{
		if (size < 2u)
		{
			return EParse::NeedMoreData;
		}

		const std::uint8_t byte0 = data[0];
		const std::uint8_t byte1 = data[1];

		const bool          fin      = (byte0 & 0x80u) != 0u;
		const std::uint8_t  rsv      = static_cast<std::uint8_t>(byte0 & 0x70u);
		const std::uint8_t  opcode   = static_cast<std::uint8_t>(byte0 & 0x0Fu);
		const bool          masked   = (byte1 & 0x80u) != 0u;
		std::uint64_t       payloadLen = static_cast<std::uint64_t>(byte1 & 0x7Fu);

		if (0u != rsv)
		{
			return EParse::Invalid; // 확장 미협상 — RSV 비트는 0이어야.
		}

		std::size_t offset = 2u;
		if (126u == payloadLen)
		{
			if (size < offset + 2u)
			{
				return EParse::NeedMoreData;
			}
			payloadLen = (static_cast<std::uint64_t>(data[offset]) << 8) |
			             (static_cast<std::uint64_t>(data[offset + 1]));
			offset += 2u;
		}
		else if (127u == payloadLen)
		{
			if (size < offset + 8u)
			{
				return EParse::NeedMoreData;
			}
			payloadLen = 0u;
			for (int i = 0; i < 8; ++i)
			{
				payloadLen = (payloadLen << 8) | static_cast<std::uint64_t>(data[offset + i]);
			}
			offset += 8u;
		}

		std::uint8_t maskBytes[4] = { 0, 0, 0, 0 };
		if (masked)
		{
			if (size < offset + 4u)
			{
				return EParse::NeedMoreData;
			}
			std::memcpy(maskBytes, data + offset, 4u);
			offset += 4u;
		}

		if (size < offset + payloadLen)
		{
			return EParse::NeedMoreData;
		}

		outFrame.Opcode = static_cast<EOpcode>(opcode);
		outFrame.Fin    = fin;
		outFrame.Payload.resize(static_cast<std::size_t>(payloadLen));
		for (std::uint64_t i = 0; i < payloadLen; ++i)
		{
			const std::uint8_t byte = data[offset + i];
			outFrame.Payload[static_cast<std::size_t>(i)] =
				masked ? static_cast<std::uint8_t>(byte ^ maskBytes[i & 3u]) : byte;
		}

		outConsumed = offset + static_cast<std::size_t>(payloadLen);
		return EParse::Ok;
	}
}
