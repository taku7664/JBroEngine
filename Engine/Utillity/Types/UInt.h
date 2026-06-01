#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

class UInt
{
public:
	constexpr UInt() noexcept = default;
	constexpr UInt(std::uint32_t value) noexcept : Value(value) {}

	constexpr operator std::uint32_t() const noexcept { return Value; }
	constexpr std::uint32_t Get() const noexcept { return Value; }
	constexpr void Set(std::uint32_t value) noexcept { Value = value; }

	UInt& operator=(std::uint32_t value) noexcept { Value = value; return *this; }
	UInt& operator+=(std::uint32_t rhs) noexcept { Value += rhs; return *this; }
	UInt& operator-=(std::uint32_t rhs) noexcept { Value -= rhs; return *this; }
	UInt& operator*=(std::uint32_t rhs) noexcept { Value *= rhs; return *this; }
	UInt& operator/=(std::uint32_t rhs) noexcept { Value /= rhs; return *this; }
	UInt& operator%=(std::uint32_t rhs) noexcept { Value %= rhs; return *this; }
	UInt& operator++() noexcept { ++Value; return *this; }
	UInt operator++(int) noexcept { UInt copy(*this); ++Value; return copy; }
	UInt& operator--() noexcept { --Value; return *this; }
	UInt operator--(int) noexcept { UInt copy(*this); --Value; return copy; }

	constexpr bool IsZero() const noexcept { return 0 == Value; }
	constexpr UInt Clamp(std::uint32_t min, std::uint32_t max) const noexcept
	{
		return UInt(Value < min ? min : (Value > max ? max : Value));
	}

	static constexpr UInt MinValue() noexcept { return UInt(std::numeric_limits<std::uint32_t>::min()); }
	static constexpr UInt MaxValue() noexcept { return UInt(std::numeric_limits<std::uint32_t>::max()); }
	static constexpr UInt Clamp(std::uint32_t value, std::uint32_t min, std::uint32_t max) noexcept
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

	std::uint32_t Value = 0;
};

static_assert(sizeof(UInt) == sizeof(std::uint32_t));
static_assert(alignof(UInt) == alignof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<UInt>);
static_assert(std::is_standard_layout_v<UInt>);

namespace std
{
	template <>
	struct hash<UInt>
	{
		std::size_t operator()(UInt value) const noexcept
		{
			return hash<std::uint32_t>()(value.Get());
		}
	};
}
