#pragma once

#include "Utillity/Types/StrongTypeOps.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

class Int
{
public:
	constexpr Int() noexcept = default;
	constexpr Int(std::int64_t value) noexcept : Value(value) {}

	constexpr operator std::int64_t() const noexcept { return Value; }
	constexpr std::int64_t Get() const noexcept { return Value; }
	constexpr void Set(std::int64_t value) noexcept { Value = value; }

	Int& operator=(std::int64_t value) noexcept { Value = value; return *this; }
	Int& operator+=(std::int64_t rhs) noexcept { Value += rhs; return *this; }
	Int& operator-=(std::int64_t rhs) noexcept { Value -= rhs; return *this; }
	Int& operator*=(std::int64_t rhs) noexcept { Value *= rhs; return *this; }
	Int& operator/=(std::int64_t rhs) noexcept { Value /= rhs; return *this; }
	Int& operator%=(std::int64_t rhs) noexcept { Value %= rhs; return *this; }
	Int& operator++() noexcept { ++Value; return *this; }
	Int operator++(int) noexcept { Int copy(*this); ++Value; return copy; }
	Int& operator--() noexcept { --Value; return *this; }
	Int operator--(int) noexcept { Int copy(*this); --Value; return copy; }

	constexpr bool IsZero() const noexcept { return 0 == Value; }
	constexpr bool IsPositive() const noexcept { return Value > 0; }
	constexpr bool IsNegative() const noexcept { return Value < 0; }
	constexpr Int Abs() const noexcept { return Int(Value < 0 ? -Value : Value); }
	constexpr Int Clamp(std::int64_t min, std::int64_t max) const noexcept
	{
		return Int(Value < min ? min : (Value > max ? max : Value));
	}

	static constexpr Int MinValue() noexcept { return Int(std::numeric_limits<std::int64_t>::min()); }
	static constexpr Int MaxValue() noexcept { return Int(std::numeric_limits<std::int64_t>::max()); }
	static constexpr Int Clamp(std::int64_t value, std::int64_t min, std::int64_t max) noexcept
	{
		return Int(value < min ? min : (value > max ? max : value));
	}

	friend constexpr Int operator+(Int lhs, Int rhs) noexcept { return Int(lhs.Value + rhs.Value); }
	friend constexpr Int operator-(Int lhs, Int rhs) noexcept { return Int(lhs.Value - rhs.Value); }
	friend constexpr Int operator*(Int lhs, Int rhs) noexcept { return Int(lhs.Value * rhs.Value); }
	friend constexpr Int operator/(Int lhs, Int rhs) noexcept { return Int(lhs.Value / rhs.Value); }
	friend constexpr Int operator%(Int lhs, Int rhs) noexcept { return Int(lhs.Value % rhs.Value); }

	friend constexpr bool operator==(Int lhs, Int rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(Int lhs, Int rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(Int lhs, Int rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(Int lhs, Int rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(Int lhs, Int rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(Int lhs, Int rhs) noexcept { return lhs.Value >= rhs.Value; }

	std::int64_t Value = 0;
};

// 기본 산술 타입과의 혼합 연산 — 없으면 `count * 2`(Int × int) 가 모호해서
// 컴파일되지 않는다. 이유와 규칙은 StrongTypeOps.h 참조.
JBRO_STRONG_MIXED_BINARY(Int, std::int64_t, +)
JBRO_STRONG_MIXED_BINARY(Int, std::int64_t, -)
JBRO_STRONG_MIXED_BINARY(Int, std::int64_t, *)
JBRO_STRONG_MIXED_BINARY(Int, std::int64_t, /)
JBRO_STRONG_MIXED_BINARY(Int, std::int64_t, %)
JBRO_STRONG_MIXED_COMPARISONS(Int, std::int64_t)

static_assert(sizeof(Int) == sizeof(std::int64_t));
static_assert(alignof(Int) == alignof(std::int64_t));
static_assert(std::is_trivially_copyable_v<Int>);
static_assert(std::is_standard_layout_v<Int>);

namespace std
{
	template <>
	struct hash<Int>
	{
		std::size_t operator()(Int value) const noexcept
		{
			return hash<std::int64_t>()(value.Get());
		}
	};
}
