#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IntegerType<T> ─ 정수 강타입의 공통 본체
//
//  Int64 / UInt64 / Int32 / UInt32 는 폭만 다르고 하는 일이 같다. 네 벌을 손으로
//  복사해 두면 고칠 일이 생길 때마다 네 군데를 맞춰야 하므로 본체를 하나만 둔다.
//  별칭(이름)은 Int.h / UInt.h 가 붙인다.
//
//  · 부호 있는 타입에만 뜻이 있는 연산(IsPositive / IsNegative / Abs)은 requires 로
//    가린다. 부호 없는 타입에서 `Value < 0` 은 항상 거짓이라 경고 대상이고, Abs 는
//    애초에 할 일이 없다. (기존 UInt 에도 이 셋은 없었다 — API 가 그대로 유지된다.)
//
//  · 기본 산술 타입과의 혼합 연산이 왜 필요한지는 StrongTypeOps.h 의 설명을 참조.
//    이 클래스는 템플릿이라 그 매크로를 그대로 쓸 수 없어(매크로는 비템플릿 타입명을
//    받는다) 같은 규칙을 아래에서 템플릿으로 편다.
//
//  · 폭이 다른 강타입끼리의 연산(Int64 + Int32)은 지원하지 않는다. 강타입 간 혼합은
//    Float/Degree/Radian 사이에서도 원래 지원하지 않는다 — 폭을 섞어야 하면 한쪽을
//    명시적으로 Get() 해서 기본 타입으로 내린다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

template<typename T>
class IntegerType
{
	static_assert(std::is_integral_v<T>, "IntegerType: T 는 정수 타입이어야 한다.");

public:
	using ValueType = T;

	constexpr IntegerType() noexcept = default;
	constexpr IntegerType(T value) noexcept : Value(value) {}

	constexpr operator T() const noexcept { return Value; }
	constexpr T Get() const noexcept { return Value; }
	constexpr void Set(T value) noexcept { Value = value; }

	IntegerType& operator=(T value) noexcept { Value = value; return *this; }
	IntegerType& operator+=(T rhs) noexcept { Value += rhs; return *this; }
	IntegerType& operator-=(T rhs) noexcept { Value -= rhs; return *this; }
	IntegerType& operator*=(T rhs) noexcept { Value *= rhs; return *this; }
	IntegerType& operator/=(T rhs) noexcept { Value /= rhs; return *this; }
	IntegerType& operator%=(T rhs) noexcept { Value %= rhs; return *this; }
	IntegerType& operator++() noexcept { ++Value; return *this; }
	IntegerType operator++(int) noexcept { IntegerType copy(*this); ++Value; return copy; }
	IntegerType& operator--() noexcept { --Value; return *this; }
	IntegerType operator--(int) noexcept { IntegerType copy(*this); --Value; return copy; }

	constexpr bool IsZero() const noexcept { return 0 == Value; }

	constexpr bool IsPositive() const noexcept requires std::is_signed_v<T> { return Value > 0; }
	constexpr bool IsNegative() const noexcept requires std::is_signed_v<T> { return Value < 0; }
	constexpr IntegerType Abs() const noexcept requires std::is_signed_v<T>
	{
		return IntegerType(Value < 0 ? -Value : Value);
	}

	constexpr IntegerType Clamp(T min, T max) const noexcept
	{
		return IntegerType(Value < min ? min : (Value > max ? max : Value));
	}

	static constexpr IntegerType MinValue() noexcept { return IntegerType(std::numeric_limits<T>::min()); }
	static constexpr IntegerType MaxValue() noexcept { return IntegerType(std::numeric_limits<T>::max()); }
	static constexpr IntegerType Clamp(T value, T min, T max) noexcept
	{
		return IntegerType(value < min ? min : (value > max ? max : value));
	}

	friend constexpr IntegerType operator+(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(lhs.Value + rhs.Value); }
	friend constexpr IntegerType operator-(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(lhs.Value - rhs.Value); }
	friend constexpr IntegerType operator*(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(lhs.Value * rhs.Value); }
	friend constexpr IntegerType operator/(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(lhs.Value / rhs.Value); }
	friend constexpr IntegerType operator%(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(lhs.Value % rhs.Value); }

	friend constexpr bool operator==(IntegerType lhs, IntegerType rhs) noexcept { return lhs.Value == rhs.Value; }
	friend constexpr bool operator!=(IntegerType lhs, IntegerType rhs) noexcept { return !(lhs == rhs); }
	friend constexpr bool operator<(IntegerType lhs, IntegerType rhs) noexcept { return lhs.Value < rhs.Value; }
	friend constexpr bool operator<=(IntegerType lhs, IntegerType rhs) noexcept { return lhs.Value <= rhs.Value; }
	friend constexpr bool operator>(IntegerType lhs, IntegerType rhs) noexcept { return lhs.Value > rhs.Value; }
	friend constexpr bool operator>=(IntegerType lhs, IntegerType rhs) noexcept { return lhs.Value >= rhs.Value; }

	T Value = 0;
};

// ── 기본 산술 타입과의 혼합 연산 ─────────────────────────────────────────────
// StrongTypeOps.h 의 JBRO_STRONG_MIXED_* 와 같은 규칙이다(양쪽 인자가 정확 일치라
// 사용자 변환이 끼는 후보들을 모두 이긴다). 관계 연산은 양방향, 동등 비교는 한
// 방향만 만든다 — 역순과 != 는 C++20 이 합성하므로 손으로 또 적으면 도로 모호해진다.
// 아래 매크로는 이 파일 안에서만 쓰고 끝에서 #undef 한다.

#define JBRO_INTEGER_MIXED_BINARY(op)                                                    \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0> \
	constexpr IntegerType<U> operator op(IntegerType<U> lhs, T rhs) noexcept              \
	{                                                                                    \
		return IntegerType<U>(lhs.Value op static_cast<U>(rhs));                         \
	}                                                                                    \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0> \
	constexpr IntegerType<U> operator op(T lhs, IntegerType<U> rhs) noexcept              \
	{                                                                                    \
		return IntegerType<U>(static_cast<U>(lhs) op rhs.Value);                         \
	}

#define JBRO_INTEGER_MIXED_RELATIONAL(op)                                                \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0> \
	constexpr bool operator op(IntegerType<U> lhs, T rhs) noexcept                        \
	{                                                                                    \
		return lhs.Value op static_cast<U>(rhs);                                         \
	}                                                                                    \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0> \
	constexpr bool operator op(T lhs, IntegerType<U> rhs) noexcept                        \
	{                                                                                    \
		return static_cast<U>(lhs) op rhs.Value;                                         \
	}

JBRO_INTEGER_MIXED_BINARY(+)
JBRO_INTEGER_MIXED_BINARY(-)
JBRO_INTEGER_MIXED_BINARY(*)
JBRO_INTEGER_MIXED_BINARY(/)
JBRO_INTEGER_MIXED_BINARY(%)

JBRO_INTEGER_MIXED_RELATIONAL(<)
JBRO_INTEGER_MIXED_RELATIONAL(<=)
JBRO_INTEGER_MIXED_RELATIONAL(>)
JBRO_INTEGER_MIXED_RELATIONAL(>=)

template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
constexpr bool operator==(IntegerType<U> lhs, T rhs) noexcept
{
	return lhs.Value == static_cast<U>(rhs);
}

#undef JBRO_INTEGER_MIXED_BINARY
#undef JBRO_INTEGER_MIXED_RELATIONAL

namespace std
{
	template <typename T>
	struct hash<IntegerType<T>>
	{
		std::size_t operator()(IntegerType<T> value) const noexcept
		{
			return hash<T>()(value.Get());
		}
	};
}
