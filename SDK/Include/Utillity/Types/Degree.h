#pragma once

#include "Utillity/Types/AngleConstants.h"
#include "Utillity/Types/StrongTypeOps.h"

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

	// 각도 × 각도 / 각도 ÷ 각도는 뜻이 없어 일부러 없다 — 스칼라와의 곱/나눗셈만
	// 클래스 밖 JBRO_STRONG_MIXED_* 로 만든다(리터럴까지 덮으려면 템플릿이어야 한다).
	friend constexpr Degree operator+(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value + rhs.Value); }
	friend constexpr Degree operator-(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value - rhs.Value); }

	friend constexpr bool operator==(Degree lhs, Degree rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(Degree lhs, Degree rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(Degree lhs, Degree rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(Degree lhs, Degree rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(Degree lhs, Degree rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(Degree lhs, Degree rhs) noexcept { return lhs.Value >= rhs.Value; }

	static constexpr Degree FromRadian(float value) noexcept { return Degree(value * RAD_TO_DEG); }

	float Value = 0.0f;
};

// 기본 산술 타입과의 혼합 연산 — 없으면 `angle + 30.0f` 가 모호해서 컴파일되지 않는다.
// 이유와 규칙은 StrongTypeOps.h 참조.
// `/` 만 좌항 전용이다 — `angle / 2` 는 각도지만 `2 / angle` 은 각도가 아니다.
JBRO_STRONG_MIXED_BINARY(Degree, float, +)
JBRO_STRONG_MIXED_BINARY(Degree, float, -)
JBRO_STRONG_MIXED_BINARY(Degree, float, *)
JBRO_STRONG_MIXED_BINARY_LHS(Degree, float, /)
JBRO_STRONG_MIXED_COMPARISONS(Degree, float)

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
