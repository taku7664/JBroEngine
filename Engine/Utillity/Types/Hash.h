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
