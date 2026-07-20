#pragma once

#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define JBRO_SIMD128_WASM 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define JBRO_SIMD128_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define JBRO_SIMD128_NEON 1
#endif

namespace Simd128
{
	inline constexpr std::size_t ByteWidth = 16;

	inline std::uint16_t MatchBytesScalar(const std::int8_t* bytes, std::int8_t value) noexcept
	{
		std::uint16_t mask = 0;
		for (std::size_t index = 0; index < ByteWidth; ++index)
		{
			if (bytes[index] == value)
			{
				mask |= static_cast<std::uint16_t>(1u << index);
			}
		}
		return mask;
	}

	inline std::uint16_t MatchBytes(const std::int8_t* bytes, std::int8_t value) noexcept
	{
#if defined(JBRO_SIMD128_WASM)
		const v128_t data = wasm_v128_load(bytes);
		const v128_t target = wasm_i8x16_splat(value);
		return static_cast<std::uint16_t>(wasm_i8x16_bitmask(wasm_i8x16_eq(data, target)));
#elif defined(JBRO_SIMD128_SSE2)
		const __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes));
		const __m128i target = _mm_set1_epi8(value);
		return static_cast<std::uint16_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(data, target)));
#elif defined(JBRO_SIMD128_NEON)
		const int8x16_t data = vld1q_s8(bytes);
		const uint8x16_t matches = vceqq_s8(data, vdupq_n_s8(value));
		alignas(16) std::uint8_t lanes[ByteWidth];
		vst1q_u8(lanes, matches);

		std::uint16_t mask = 0;
		for (std::size_t index = 0; index < ByteWidth; ++index)
		{
			mask |= static_cast<std::uint16_t>((lanes[index] >> 7) << index);
		}
		return mask;
#else
		return MatchBytesScalar(bytes, value);
#endif
	}

	inline unsigned FirstMatchIndex(std::uint16_t mask) noexcept
	{
		if (0 == mask)
		{
			return static_cast<unsigned>(ByteWidth);
		}

#if defined(_MSC_VER)
		unsigned long index = 0;
		_BitScanForward(&index, mask);
		return static_cast<unsigned>(index);
#else
		return static_cast<unsigned>(__builtin_ctz(static_cast<unsigned>(mask)));
#endif
	}
}
