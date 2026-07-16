#pragma once

#include "Utillity/Types/Degree.h"

#include <cmath>
#include <functional>
#include <type_traits>

class Radian
{
public:
	constexpr Radian() noexcept = default;
	constexpr Radian(float value) noexcept : Value(value) {}
	constexpr Radian(Degree degree) noexcept : Value(degree.Get() * DEG_TO_RAD) {}

	constexpr operator float() const noexcept { return Value; }
	constexpr float Get() const noexcept { return Value; }
	constexpr void Set(float value) noexcept { Value = value; }

	constexpr Degree ToDegree() const noexcept { return Degree(Value * RAD_TO_DEG); }
	constexpr Radian ToRadian() const noexcept { return *this; }

	Radian NormalizedTwoPi() const
	{
		constexpr float twoPi = PI * 2.0f;
		float value = std::fmod(Value, twoPi);
		if (value < 0.0f)
		{
			value += twoPi;
		}
		return Radian(value);
	}

	Radian NormalizedPi() const
	{
		float value = NormalizedTwoPi().Value;
		if (value > PI)
		{
			value -= PI * 2.0f;
		}
		return Radian(value);
	}

	Radian& operator=(float value) noexcept { Value = value; return *this; }
	Radian& operator=(Degree degree) noexcept { Value = degree.Get() * DEG_TO_RAD; return *this; }
	Radian& operator+=(Radian rhs) noexcept { Value += rhs.Value; return *this; }
	Radian& operator-=(Radian rhs) noexcept { Value -= rhs.Value; return *this; }
	Radian& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
	Radian& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

	// 각도 × 각도 / 각도 ÷ 각도는 뜻이 없어 일부러 없다 — 스칼라와의 곱/나눗셈만
	// 클래스 밖 JBRO_STRONG_MIXED_* 로 만든다(리터럴까지 덮으려면 템플릿이어야 한다).
	friend constexpr Radian operator+(Radian lhs, Radian rhs) noexcept { return Radian(lhs.Value + rhs.Value); }
	friend constexpr Radian operator-(Radian lhs, Radian rhs) noexcept { return Radian(lhs.Value - rhs.Value); }

	friend constexpr bool operator==(Radian lhs, Radian rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(Radian lhs, Radian rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(Radian lhs, Radian rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(Radian lhs, Radian rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(Radian lhs, Radian rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(Radian lhs, Radian rhs) noexcept { return lhs.Value >= rhs.Value; }

	static constexpr Radian FromDegree(float value) noexcept { return Radian(value * DEG_TO_RAD); }

	float Value = 0.0f;
};

constexpr Degree::Degree(const Radian& radian) noexcept : Value(radian.Get() * RAD_TO_DEG) {}
constexpr Radian Degree::ToRadian() const noexcept { return Radian(Value * DEG_TO_RAD); }

// 기본 산술 타입과의 혼합 연산 — Degree 와 같은 이유·같은 범위(StrongTypeOps.h 참조).
JBRO_STRONG_MIXED_BINARY(Radian, float, +)
JBRO_STRONG_MIXED_BINARY(Radian, float, -)
JBRO_STRONG_MIXED_BINARY(Radian, float, *)
JBRO_STRONG_MIXED_BINARY_LHS(Radian, float, /)
JBRO_STRONG_MIXED_COMPARISONS(Radian, float)

static_assert(sizeof(Radian) == sizeof(float));
static_assert(alignof(Radian) == alignof(float));
static_assert(std::is_trivially_copyable_v<Radian>);
static_assert(std::is_standard_layout_v<Radian>);

namespace std
{
	template <>
	struct hash<Radian>
	{
		std::size_t operator()(Radian value) const noexcept
		{
			return hash<float>()(value.Get());
		}
	};
}
