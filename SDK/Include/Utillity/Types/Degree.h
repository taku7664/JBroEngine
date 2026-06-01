#pragma once

#include "Utillity/Types/AngleConstants.h"

#include <cmath>
#include <functional>
#include <type_traits>

class Radian;

class Degree
{
public:
	constexpr Degree() noexcept = default;
	constexpr Degree(float value) noexcept : Value(value) {}
	constexpr Degree(const Radian& radian) noexcept;

	constexpr operator float() const noexcept { return Value; }
	constexpr float Get() const noexcept { return Value; }
	constexpr void Set(float value) noexcept { Value = value; }

	constexpr Radian ToRadian() const noexcept;
	constexpr Degree ToDegree() const noexcept { return *this; }

	Degree Normalized360() const
	{
		float value = std::fmod(Value, 360.0f);
		if (value < 0.0f)
		{
			value += 360.0f;
		}
		return Degree(value);
	}

	Degree Normalized180() const
	{
		float value = Normalized360().Value;
		if (value > 180.0f)
		{
			value -= 360.0f;
		}
		return Degree(value);
	}

	Degree& operator=(float value) noexcept { Value = value; return *this; }
	Degree& operator+=(Degree rhs) noexcept { Value += rhs.Value; return *this; }
	Degree& operator-=(Degree rhs) noexcept { Value -= rhs.Value; return *this; }
	Degree& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
	Degree& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

	friend constexpr Degree operator+(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value + rhs.Value); }
	friend constexpr Degree operator-(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value - rhs.Value); }
	friend constexpr Degree operator*(Degree lhs, float rhs) noexcept { return Degree(lhs.Value * rhs); }
	friend constexpr Degree operator*(float lhs, Degree rhs) noexcept { return Degree(lhs * rhs.Value); }
	friend constexpr Degree operator/(Degree lhs, float rhs) noexcept { return Degree(lhs.Value / rhs); }

	friend constexpr bool operator==(Degree lhs, Degree rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(Degree lhs, Degree rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(Degree lhs, Degree rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(Degree lhs, Degree rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(Degree lhs, Degree rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(Degree lhs, Degree rhs) noexcept { return lhs.Value >= rhs.Value; }

	static constexpr Degree FromRadian(float value) noexcept { return Degree(value * RAD_TO_DEG); }

	float Value = 0.0f;
};

static_assert(sizeof(Degree) == sizeof(float));
static_assert(alignof(Degree) == alignof(float));
static_assert(std::is_trivially_copyable_v<Degree>);
static_assert(std::is_standard_layout_v<Degree>);

namespace std
{
	template <>
	struct hash<Degree>
	{
		std::size_t operator()(Degree value) const noexcept
		{
			return hash<float>()(value.Get());
		}
	};
}

#include "Utillity/Types/Radian.h"
