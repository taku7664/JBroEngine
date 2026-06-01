#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

class UInt
{
public:
	constexpr UInt() noexcept = default;
	constexpr UInt(std::uint64_t value) noexcept : Value(value) {}

	constexpr operator std::uint64_t() const noexcept { return Value; }
	constexpr std::uint64_t Get() const noexcept { return Value; }
	constexpr void Set(std::uint64_t value) noexcept { Value = value; }

	UInt& operator=(std::uint64_t value) noexcept { Value = value; return *this; }
	UInt& operator+=(std::uint64_t rhs) noexcept { Value += rhs; return *this; }
	UInt& operator-=(std::uint64_t rhs) noexcept { Value -= rhs; return *this; }
	UInt& operator*=(std::uint64_t rhs) noexcept { Value *= rhs; return *this; }
	UInt& operator/=(std::uint64_t rhs) noexcept { Value /= rhs; return *this; }
	UInt& operator%=(std::uint64_t rhs) noexcept { Value %= rhs; return *this; }
	UInt& operator++() noexcept { ++Value; return *this; }
	UInt operator++(int) noexcept { UInt copy(*this); ++Value; return copy; }
	UInt& operator--() noexcept { --Value; return *this; }
	UInt operator--(int) noexcept { UInt copy(*this); --Value; return copy; }

	constexpr bool IsZero() const noexcept { return 0 == Value; }
	constexpr UInt Clamp(std::uint64_t min, std::uint64_t max) const noexcept
	{
		return UInt(Value < min ? min : (Value > max ? max : Value));
	}

	static constexpr UInt MinValue() noexcept { return UInt(std::numeric_limits<std::uint64_t>::min()); }
	static constexpr UInt MaxValue() noexcept { return UInt(std::numeric_limits<std::uint64_t>::max()); }
	static constexpr UInt Clamp(std::uint64_t value, std::uint64_t min, std::uint64_t max) noexcept
	{
		return UInt(value < min ? min : (value > max ? max : value));
	}

	friend constexpr UInt operator+(UInt lhs, UInt rhs) noexcept { return UInt(lhs.Value + rhs.Value); }
	friend constexpr UInt operator-(UInt lhs, UInt rhs) noexcept { return UInt(lhs.Value - rhs.Value); }
	friend constexpr UInt operator*(UInt lhs, UInt rhs) noexcept { return UInt(lhs.Value * rhs.Value); }
	friend constexpr UInt operator/(UInt lhs, UInt rhs) noexcept { return UInt(lhs.Value / rhs.Value); }
	friend constexpr UInt operator%(UInt lhs, UInt rhs) noexcept { return UInt(lhs.Value % rhs.Value); }

	friend constexpr bool operator==(UInt lhs, UInt rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(UInt lhs, UInt rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(UInt lhs, UInt rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(UInt lhs, UInt rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(UInt lhs, UInt rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(UInt lhs, UInt rhs) noexcept { return lhs.Value >= rhs.Value; }

	std::uint64_t Value = 0;
};

static_assert(sizeof(UInt) == sizeof(std::uint64_t));
static_assert(alignof(UInt) == alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<UInt>);
static_assert(std::is_standard_layout_v<UInt>);

namespace std
{
	template <>
	struct hash<UInt>
	{
		std::size_t operator()(UInt value) const noexcept
		{
			return hash<std::uint64_t>()(value.Get());
		}
	};
}
