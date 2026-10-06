#pragma once

#include <JBro/Types/IntegerType.h>
#include <JBro/Types/StrongTypeOps.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

namespace JBro
{
class Float
{
public:
	constexpr Float() noexcept = default;
	// **실수에서만 암시로 만든다**(D-290). 정수·열거형에서는 명시다(`static_cast<Float>(count)`) - 정수 리터럴이
	// `Float` 로도 정수 강타입으로도 암시 변환되면 `Range(3, 7)` 이 모호해진다. `Float speed = 0;` 은 `0.0f` 로 적는다.
	template<typename A>
		requires std::is_floating_point_v<A>
	constexpr Float(A value) noexcept : Value(static_cast<float>(value)) {}

	template<typename A>
		requires (std::is_integral_v<A> || std::is_enum_v<A>)
	constexpr explicit Float(A value) noexcept : Value(static_cast<float>(value)) {}
	// 정수 강타입에서 바로 온다. 원시 `int` 가 `float` 로 암시 변환되던 것과 같다 -
	// 없으면 `Int32` → `int` → `float` → `Float` 로 사용자 변환이 둘이라 막힌다(D-290).
	template<typename U>
	constexpr Float(IntegerType<U> value) noexcept : Value(static_cast<float>(value.Get())) {}

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

	// `float` 를 받는 대입을 따로 두지 않는다. 암시 생성자 `Float(float)` 와 복사 대입이 그 일을 하고,
	// 따로 두면 `angle = radian` 이 `operator=(float)` 와 복사 대입(각도 → Float) 사이에서 모호해진다(D-290).

	constexpr Float& operator+=(float rhs) noexcept { Value += rhs; return *this; }
	constexpr Float& operator-=(float rhs) noexcept { Value -= rhs; return *this; }
	constexpr Float& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
	constexpr Float& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

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

// 기본 산술 타입과의 혼합 연산 — 없으면 `speed * dt`(Float × float) 가 모호해서
// 컴파일되지 않는다. 이유와 규칙은 StrongTypeOps.h 참조.
JBRO_STRONG_MIXED_BINARY(Float, float, +)
JBRO_STRONG_MIXED_BINARY(Float, float, -)
JBRO_STRONG_MIXED_BINARY(Float, float, *)
JBRO_STRONG_MIXED_BINARY(Float, float, /)
JBRO_STRONG_MIXED_COMPARISONS(Float, float)

static_assert(sizeof(Float) == sizeof(float));
static_assert(alignof(Float) == alignof(float));
static_assert(std::is_trivially_copyable_v<Float>);
static_assert(std::is_standard_layout_v<Float>);
}

namespace std
{
	template <>
	struct hash<JBro::Float>
	{
		std::size_t operator()(JBro::Float value) const noexcept
		{
			return hash<float>()(value.Get());
		}
	};
}
