#pragma once

#include <cstddef>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  Guid128 — 128비트 고유 식별자의 정수 표현.
//
//  File::GenerateGuid() 이 만드는 32-hex 문자열(상위 16 hex = Hi, 하위 16 hex = Lo)과
//  무손실로 왕복한다. 해시가 아니라 원래 128비트를 그대로 담으므로 충돌이 없다.
//
//  File::Guid(std::filesystem::path) 와 달리 힙 할당·UTF8↔UTF16 트랜스코딩이 없어
//  Ref 해석 같은 hot path 의 조회 키로 적합하다. POD(uint64 2개)라 호스트↔게임 DLL
//  경계에도 안전하다.
//
//  디스크에는 여전히 hex 문자열로 저장한다(로드 시 FromText, 저장 시 ToText) — 기존
//  프로젝트 파일과 호환된다.
// ─────────────────────────────────────────────────────────────────────────────
struct Guid128
{
	std::uint64_t Hi = 0;
	std::uint64_t Lo = 0;

	bool IsNull() const { return 0 == Hi && 0 == Lo; }
	bool operator==(const Guid128& rhs) const { return Hi == rhs.Hi && Lo == rhs.Lo; }
	bool operator!=(const Guid128& rhs) const { return false == (*this == rhs); }

	// 32-hex 문자열(utf8, null 종료) → Guid128. 앞 16 nibble = Hi, 다음 16 = Lo.
	// 16진수가 아닌 문자는 건너뛰고, nibble 이 32개보다 적으면 상위가 0으로 남는다
	// (GenerateGuid 는 항상 32자 zero-padding 이라 그대로 왕복). 빈 문자열 → null.
	static Guid128 FromText(const char* text)
	{
		Guid128 result;
		if (nullptr == text)
		{
			return result;
		}
		std::uint64_t* target = &result.Hi;
		int nibbleCount = 0;
		for (std::size_t i = 0; '\0' != text[i]; ++i)
		{
			const int value = HexValue(text[i]);
			if (value < 0)
			{
				continue;
			}
			*target = (*target << 4) | static_cast<std::uint64_t>(value);
			if (16 == ++nibbleCount)
			{
				target = &result.Lo;
				nibbleCount = 0;
			}
		}
		return result;
	}

	// Guid128 → 32-hex 소문자 문자열(널 종료). out 은 최소 33바이트.
	void ToText(char out[33]) const
	{
		WriteHex16(out, Hi);
		WriteHex16(out + 16, Lo);
		out[32] = '\0';
	}

private:
	static int HexValue(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	static void WriteHex16(char* out, std::uint64_t value)
	{
		static const char digits[] = "0123456789abcdef";
		for (int i = 15; i >= 0; --i)
		{
			out[i] = digits[value & 0xFull];
			value >>= 4;
		}
	}
};

namespace std
{
	template<>
	struct hash<Guid128>
	{
		std::size_t operator()(const Guid128& guid) const noexcept
		{
			std::uint64_t mixed = guid.Hi ^ (guid.Lo + 0x9e3779b97f4a7c15ull + (guid.Hi << 6) + (guid.Hi >> 2));
			return static_cast<std::size_t>(mixed);
		}
	};
}
