#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>

template<typename T>
struct Hash
{
	using IsTransparent = void;

	std::size_t operator()(const T& value) const
		noexcept(noexcept(std::hash<T>{}(value)))
	{
		return std::hash<T>{}(value);
	}
};
// 해시값 결합(boost hash_combine 의 상수를 size_t 폭에 맞춘 변형). 페어/복합 키의
// unordered 컨테이너 해시를 만들 때 사용한다 — 좁은 정수에 비트 패킹으로 키를 합성하면
// 비트 유실로 서로 다른 키가 충돌할 수 있으므로, 키는 원본 그대로 두고 해시만 결합한다.
// 결합 결과는 런타임 전용이다(직렬화 금지 — 플랫폼별 size_t 폭에 따라 달라진다).
constexpr void HashCombine(std::size_t& seed, std::size_t value) noexcept
{
	constexpr std::size_t goldenRatio = static_cast<std::size_t>(0x9E3779B97F4A7C15ull);
	seed ^= value + goldenRatio + (seed << 6) + (seed >> 2);
}

template<typename T = void>
struct EqualTo
{
	using IsTransparent = void;

	template<typename Lhs, typename Rhs>
	constexpr bool operator()(const Lhs& lhs, const Rhs& rhs) const
		noexcept(noexcept(lhs == rhs))
	{
		return lhs == rhs;
	}
};
