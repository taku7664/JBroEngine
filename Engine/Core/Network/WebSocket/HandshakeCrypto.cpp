#include "pch.h"
#include "HandshakeCrypto.h"

#include <cstring>
#include <vector>

namespace WebSocket
{
	namespace
	{
		// ── SHA-1 (RFC 3174) ───────────────────────────────────────────────────────
		// 20바이트 다이제스트. WebSocket 핸드셰이크 accept-key 전용.

		inline std::uint32_t RotL(std::uint32_t value, int bits)
		{
			return (value << bits) | (value >> (32 - bits));
		}

		void Sha1(const std::uint8_t* message, std::size_t length, std::uint8_t outDigest[20])
		{
			std::uint32_t h0 = 0x67452301u;
			std::uint32_t h1 = 0xEFCDAB89u;
			std::uint32_t h2 = 0x98BADCFEu;
			std::uint32_t h3 = 0x10325476u;
			std::uint32_t h4 = 0xC3D2E1F0u;

			// 패딩: 0x80 이어 붙이고, 길이가 56 mod 64 가 되도록 0 채운 뒤 64비트 비트길이(빅엔디언).
			const std::uint64_t bitLength = static_cast<std::uint64_t>(length) * 8u;

			std::vector<std::uint8_t> data(message, message + length);
			data.push_back(0x80u);
			while ((data.size() % 64u) != 56u)
			{
				data.push_back(0x00u);
			}
			for (int i = 7; i >= 0; --i)
			{
				data.push_back(static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xFFu));
			}

			for (std::size_t chunk = 0; chunk < data.size(); chunk += 64u)
			{
				std::uint32_t w[80];
				for (int i = 0; i < 16; ++i)
				{
					const std::size_t base = chunk + static_cast<std::size_t>(i) * 4u;
					w[i] = (static_cast<std::uint32_t>(data[base + 0]) << 24) |
					       (static_cast<std::uint32_t>(data[base + 1]) << 16) |
					       (static_cast<std::uint32_t>(data[base + 2]) << 8) |
					       (static_cast<std::uint32_t>(data[base + 3]));
				}
				for (int i = 16; i < 80; ++i)
				{
					w[i] = RotL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
				}

				std::uint32_t a = h0;
				std::uint32_t b = h1;
				std::uint32_t c = h2;
				std::uint32_t d = h3;
				std::uint32_t e = h4;

				for (int i = 0; i < 80; ++i)
				{
					std::uint32_t f = 0;
					std::uint32_t k = 0;
					if (i < 20)
					{
						f = (b & c) | ((~b) & d);
						k = 0x5A827999u;
					}
					else if (i < 40)
					{
						f = b ^ c ^ d;
						k = 0x6ED9EBA1u;
					}
					else if (i < 60)
					{
						f = (b & c) | (b & d) | (c & d);
						k = 0x8F1BBCDCu;
					}
					else
					{
						f = b ^ c ^ d;
						k = 0xCA62C1D6u;
					}

					const std::uint32_t temp = RotL(a, 5) + f + e + k + w[i];
					e = d;
					d = c;
					c = RotL(b, 30);
					b = a;
					a = temp;
				}

				h0 += a;
				h1 += b;
				h2 += c;
				h3 += d;
				h4 += e;
			}

			const std::uint32_t hs[5] = { h0, h1, h2, h3, h4 };
			for (int i = 0; i < 5; ++i)
			{
				outDigest[i * 4 + 0] = static_cast<std::uint8_t>((hs[i] >> 24) & 0xFFu);
				outDigest[i * 4 + 1] = static_cast<std::uint8_t>((hs[i] >> 16) & 0xFFu);
				outDigest[i * 4 + 2] = static_cast<std::uint8_t>((hs[i] >> 8) & 0xFFu);
				outDigest[i * 4 + 3] = static_cast<std::uint8_t>((hs[i]) & 0xFFu);
			}
		}

		constexpr char kBase64Alphabet[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		// WebSocket 핸드셰이크 매직 GUID (RFC6455 §1.3).
		constexpr char kMagicGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	}

	std::string Base64Encode(const std::uint8_t* data, std::size_t size)
	{
		std::string out;
		out.reserve(((size + 2u) / 3u) * 4u);

		std::size_t i = 0;
		while (i + 3u <= size)
		{
			const std::uint32_t triple =
				(static_cast<std::uint32_t>(data[i]) << 16) |
				(static_cast<std::uint32_t>(data[i + 1]) << 8) |
				(static_cast<std::uint32_t>(data[i + 2]));
			out.push_back(kBase64Alphabet[(triple >> 18) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple >> 12) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple >> 6) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple) & 0x3Fu]);
			i += 3u;
		}

		const std::size_t remaining = size - i;
		if (1u == remaining)
		{
			const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
			out.push_back(kBase64Alphabet[(triple >> 18) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple >> 12) & 0x3Fu]);
			out.push_back('=');
			out.push_back('=');
		}
		else if (2u == remaining)
		{
			const std::uint32_t triple =
				(static_cast<std::uint32_t>(data[i]) << 16) |
				(static_cast<std::uint32_t>(data[i + 1]) << 8);
			out.push_back(kBase64Alphabet[(triple >> 18) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple >> 12) & 0x3Fu]);
			out.push_back(kBase64Alphabet[(triple >> 6) & 0x3Fu]);
			out.push_back('=');
		}

		return out;
	}

	std::string ComputeAcceptKey(const std::string& clientKey)
	{
		std::string combined = clientKey;
		combined += kMagicGuid;

		std::uint8_t digest[20];
		Sha1(reinterpret_cast<const std::uint8_t*>(combined.data()), combined.size(), digest);
		return Base64Encode(digest, sizeof(digest));
	}

	std::string GenerateClientKey(std::uint64_t seed)
	{
		// xorshift64 로 16바이트 논스 생성(예측 난수 — 보안 목적 아님).
		std::uint64_t state = (0 != seed) ? seed : 0x9E3779B97F4A7C15ull;
		std::uint8_t nonce[16];
		for (int i = 0; i < 16; i += 8)
		{
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			for (int b = 0; b < 8; ++b)
			{
				nonce[i + b] = static_cast<std::uint8_t>((state >> (b * 8)) & 0xFFu);
			}
		}
		return Base64Encode(nonce, sizeof(nonce));
	}
}
