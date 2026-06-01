#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

class Float
{
public:
	constexpr Float() noexcept = default;
	constexpr Float(float value) noexcept : Value(value) {}

	constexpr operator float() const noexcept { return Value; }
	constexpr float Get() const noexcept { return Value; }
	constexpr void Set(float value) noexcept { Value = value; }

	float Floor() const { return std::floor(Value); }
	float Ceil() const { return std::ceil(Value); }
	float Round() const { return std::round(Value); }
	float Trunc() const { return std::trunc(Value); }
	Float Truncate(int digits) const { return Float(CutDecimal(Value, digits)); }
	Float Clamp(float min, float max) const { return Float(std::clamp(Value, min, max)); }
	Float Abs() const { return Float(std::fabs(Value)); }
	bool IsFinite() const { return std::isfinite(Value); }
	bool IsNearlyZero(float epsilon = 0.00001f) const { return std::fabs(Value) <= epsilon; }
	bool NearlyEquals(float rhs, float epsilon = 0.00001f) const { return std::fabs(Value - rhs) <= epsilon; }

	Float& operator=(float value) noexcept
	{
		Value = value;
		return *this;
	}

	Float& operator+=(float rhs) noexcept { Value += rhs; return *this; }
	Float& operator-=(float rhs) noexcept { Value -= rhs; return *this; }
	Float& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
	Float& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

	friend constexpr Float operator+(Float lhs, Float rhs) noexcept { return Float(lhs.Value + rhs.Value); }
	friend constexpr Float operator-(Float lhs, Float rhs) noexcept { return Float(lhs.Value - rhs.Value); }
	friend constexpr Float operator*(Float lhs, Float rhs) noexcept { return Float(lhs.Value * rhs.Value); }
	friend constexpr Float operator/(Float lhs, Float rhs) noexcept { return Float(lhs.Value / rhs.Value); }

	friend constexpr bool operator==(Float lhs, Float rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(Float lhs, Float rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(Float lhs, Float rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(Float lhs, Float rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(Float lhs, Float rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(Float lhs, Float rhs) noexcept { return lhs.Value >= rhs.Value; }

	static float Floor(float value) { return std::floor(value); }
	static float Ceil(float value) { return std::ceil(value); }
	static float Round(float value) { return std::round(value); }
	static float Trunc(float value) { return std::trunc(value); }
	static float Abs(float value) { return std::fabs(value); }
	static float Clamp(float value, float min, float max) { return std::clamp(value, min, max); }
	static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	static bool IsFinite(float value) { return std::isfinite(value); }
	static bool NearlyEquals(float a, float b, float epsilon = 0.00001f) { return std::fabs(a - b) <= epsilon; }

	static float CutDecimal(float value, int digits)
	{
		if (digits <= 0)
		{
			return std::trunc(value);
		}

		float scale = 1.0f;
		for (int i = 0; i < digits; ++i)
		{
			scale *= 10.0f;
		}
		return std::trunc(value * scale) / scale;
	}

	float Value = 0.0f;
};

static_assert(sizeof(Float) == sizeof(float));
static_assert(alignof(Float) == alignof(float));
static_assert(std::is_trivially_copyable_v<Float>);
static_assert(std::is_standard_layout_v<Float>);

namespace std
{
	template <>
	struct hash<Float>
	{
		std::size_t operator()(Float value) const noexcept
		{
			return hash<float>()(value.Get());
		}
	};
}
