#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

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

namespace JBro
{
template<typename T>
class IntegerType
{
	static_assert(std::is_integral_v<T>, "IntegerType: T 는 정수 타입이어야 한다.");

public:
	using ValueType = T;

	constexpr IntegerType() noexcept = default;
	// **같은 종류에서만 암시로 만든다**(D-290). 정수 리터럴과 같거나 좁은 원시 정수·범위 없는 열거형은 암시(`UInt32 count = 0;`),
	// 넓은 정수·실수·열거형·bool 은 명시다(`static_cast<UInt32>(size)`). 실수가 정수 강타입으로 암시 변환되면
	// `Range(Int32, Int32)` 와 `Range(Float, Float)` 가 `Range(1.0f, 2.0f)` 에서 둘 다 닿아 모호해진다 - 종류가 갈리면
	// 리터럴이 맞는 판을 저절로 고른다. 좁히기도 명시라야 원시 타입의 폭 경고가 잡던 것을 컴파일러가 잡는다.
	template<typename A>
		requires (((std::is_integral_v<A> && !std::is_same_v<A, bool>) || (std::is_enum_v<A> && std::is_convertible_v<A, long long>)) && sizeof(A) <= sizeof(T))
	constexpr IntegerType(A value) noexcept : Value(static_cast<T>(value)) {}

	template<typename A>
		requires ((std::is_arithmetic_v<A> || std::is_enum_v<A>)
			&& !(((std::is_integral_v<A> && !std::is_same_v<A, bool>) || (std::is_enum_v<A> && std::is_convertible_v<A, long long>)) && sizeof(A) <= sizeof(T)))
	constexpr explicit IntegerType(A value) noexcept : Value(static_cast<T>(value)) {}

	// `Float` 처럼 원시 수로 바뀌는 엔진 값 타입에서 명시적으로 만든다(정수 강타입끼리는 위의 넓히기 생성자가 맡는다).
	template<typename A>
		requires (std::is_class_v<A> && !requires(A a) { a.Value; typename A::ValueType; }
			&& std::is_convertible_v<A, double>)
	constexpr explicit IntegerType(const A& value) noexcept : Value(static_cast<T>(static_cast<double>(value))) {}


	// **값을 잃지 않는 넓히기는 암시로 된다**(`UInt32` → `UInt64`, `Int32` → `Int64`, `UInt32` → `Int64`).
	// 원시 정수끼리 되던 것이 강타입이라고 막히면 필드를 옮긴 자리마다 캐스트가 붙는다(D-290).
	// 좁히기와 부호가 바뀌는 쪽은 명시적이다 - 원시 타입의 경고가 잡던 것을 여기서는 컴파일러가 잡는다.
	template<typename U>
		requires (!std::is_same_v<U, T>)
	constexpr explicit(!(sizeof(U) < sizeof(T) && (std::is_signed_v<U> == std::is_signed_v<T> || std::is_unsigned_v<U>)))
		IntegerType(IntegerType<U> other) noexcept
		: Value(static_cast<T>(other.Value))
	{
	}

	constexpr operator T() const noexcept { return Value; }
	constexpr T Get() const noexcept { return Value; }
	constexpr void Set(T value) noexcept { Value = value; }

	// 원시 값을 받는 대입은 따로 두지 않는다. 암시 생성자와 복사 대입이 그 일을 하고, 따로 두면
	// `UInt64 = UInt32` 가 `operator=(T)` 와 넓히기 생성자 사이에서 모호해진다(D-290).
	constexpr IntegerType& operator+=(T rhs) noexcept { Value += rhs; return *this; }
	constexpr IntegerType& operator-=(T rhs) noexcept { Value -= rhs; return *this; }
	constexpr IntegerType& operator*=(T rhs) noexcept { Value *= rhs; return *this; }
	constexpr IntegerType& operator/=(T rhs) noexcept { Value /= rhs; return *this; }
	constexpr IntegerType& operator%=(T rhs) noexcept { Value %= rhs; return *this; }
	constexpr IntegerType& operator++() noexcept { ++Value; return *this; }
	constexpr IntegerType operator++(int) noexcept { IntegerType copy(*this); ++Value; return copy; }
	constexpr IntegerType& operator--() noexcept { --Value; return *this; }
	constexpr IntegerType operator--(int) noexcept { IntegerType copy(*this); --Value; return copy; }

	// 비트 연산이다. 해시·플래그·마스크가 정수 필드를 엔진 타입으로 옮긴 뒤에도 그대로 읽히게 한다(D-290).
	constexpr IntegerType& operator&=(T rhs) noexcept { Value &= rhs; return *this; }
	constexpr IntegerType& operator|=(T rhs) noexcept { Value |= rhs; return *this; }
	constexpr IntegerType& operator^=(T rhs) noexcept { Value ^= rhs; return *this; }
	template<typename S, std::enable_if_t<std::is_integral_v<S>, int> = 0>
	constexpr IntegerType& operator<<=(S shift) noexcept { Value = static_cast<T>(Value << shift); return *this; }
	template<typename S, std::enable_if_t<std::is_integral_v<S>, int> = 0>
	constexpr IntegerType& operator>>=(S shift) noexcept { Value = static_cast<T>(Value >> shift); return *this; }
	friend constexpr IntegerType operator&(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(static_cast<T>(lhs.Value & rhs.Value)); }
	friend constexpr IntegerType operator|(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(static_cast<T>(lhs.Value | rhs.Value)); }
	friend constexpr IntegerType operator^(IntegerType lhs, IntegerType rhs) noexcept { return IntegerType(static_cast<T>(lhs.Value ^ rhs.Value)); }
	friend constexpr IntegerType operator~(IntegerType value) noexcept { return IntegerType(static_cast<T>(~value.Value)); }
	template<typename S, std::enable_if_t<std::is_integral_v<S>, int> = 0>
	friend constexpr IntegerType operator<<(IntegerType value, S shift) noexcept { return IntegerType(static_cast<T>(value.Value << shift)); }
	template<typename S, std::enable_if_t<std::is_integral_v<S>, int> = 0>
	friend constexpr IntegerType operator>>(IntegerType value, S shift) noexcept { return IntegerType(static_cast<T>(value.Value >> shift)); }

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

// **실수와 섞으면 실수로 계산한다.** 예전에는 실수 쪽을 정수로 잘라 `Int32(10) * 0.5f` 가 0 이었고
// `count < 0.5f` 가 `count < 0` 이었다 - 기본 타입이라면 `10 * 0.5f` 는 5.0f 다. 정수 필드를 엔진 타입으로
// 옮기면(D-290) 그런 식이 곳곳에 생기므로, 정수끼리만 강타입으로 남기고 실수가 끼면 그 실수 타입으로 돌려준다.
// **더 넓은 원시 정수와 섞으면 넓은 쪽으로 받는다.** 예전에는 강타입 폭으로 잘라
// `uint64 * UInt32` 가 32 비트로 잘렸다(PCG32 의 곱이 그 모양이다). 같은 폭이거나 좁은 리터럴
// (`count + 1`, `index - 1u`)은 강타입 폭 그대로다 - 원시 타입이라면 부호가 바뀔 자리지만
// 필드를 옮긴 코드가 `Int32 + 1u` 마다 깨지지 않게 한다.
// 정수처럼 섞이는 원시 타입: 정수와 **범위 없는** 열거형(`ImGuiKey`, 플래그 열거형). 범위 있는 열거형은
// 정수로 암시 변환되지 않으므로 여기 들지 않는다 - 원시 타입에서도 섞이지 않던 것이다.
template<typename T, bool = std::is_enum_v<T>>
struct IsIntegerLike : std::bool_constant<std::is_integral_v<T>>
{
};

template<typename T>
struct IsIntegerLike<T, true> : std::bool_constant<std::is_convertible_v<T, std::underlying_type_t<T>>>
{
};

// `std::cmp_less` 따위는 문자 타입과 bool 을 받지 않는다. 같은 폭의 정수로 옮긴다.
template<typename T, bool = std::is_enum_v<T>>
struct IntegerRawOf
{
	using Type = std::conditional_t<std::is_same_v<T, char>, signed char,
		std::conditional_t<std::is_same_v<T, char8_t>, unsigned char,
		std::conditional_t<std::is_same_v<T, char16_t> || std::is_same_v<T, wchar_t>, std::uint16_t,
		std::conditional_t<std::is_same_v<T, char32_t>, std::uint32_t, T>>>>;
};

template<typename T>
struct IntegerRawOf<T, true>
{
	using Type = std::underlying_type_t<T>;
};

template<typename U, typename T>
struct IntegerMixResult
{
	using Wide = std::common_type_t<U, typename IntegerRawOf<T>::Type>;
	using Fixed = std::conditional_t<std::is_signed_v<Wide>,
		std::conditional_t<sizeof(Wide) == 8, std::int64_t, std::int32_t>,
		std::conditional_t<sizeof(Wide) == 8, std::uint64_t, std::uint32_t>>;
	using Type = std::conditional_t<(sizeof(Wide) > sizeof(U)), Fixed, U>;
};

#define JBRO_INTEGER_MIXED_BINARY(op)                                                    \
	template<typename U, typename T, std::enable_if_t<IsIntegerLike<T>::value, int> = 0>   \
	constexpr IntegerType<typename IntegerMixResult<U, T>::Type> operator op(IntegerType<U> lhs, T rhs) noexcept \
	{                                                                                    \
		using R = typename IntegerMixResult<U, T>::Type;                                 \
		return IntegerType<R>(static_cast<R>(static_cast<R>(lhs.Value) op static_cast<R>(rhs))); \
	}                                                                                    \
	template<typename U, typename T, std::enable_if_t<IsIntegerLike<T>::value, int> = 0>   \
	constexpr IntegerType<typename IntegerMixResult<U, T>::Type> operator op(T lhs, IntegerType<U> rhs) noexcept \
	{                                                                                    \
		using R = typename IntegerMixResult<U, T>::Type;                                 \
		return IntegerType<R>(static_cast<R>(static_cast<R>(lhs) op static_cast<R>(rhs.Value))); \
	}

#define JBRO_INTEGER_MIXED_FLOATING(op)                                                  \
	template<typename U, typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0> \
	constexpr T operator op(IntegerType<U> lhs, T rhs) noexcept                           \
	{                                                                                    \
		return static_cast<T>(lhs.Value) op rhs;                                         \
	}                                                                                    \
	template<typename U, typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0> \
	constexpr T operator op(T lhs, IntegerType<U> rhs) noexcept                           \
	{                                                                                    \
		return lhs op static_cast<T>(rhs.Value);                                         \
	}

// 정수끼리의 비교는 **값으로** 한다(`std::cmp_less` 따위). 강타입 폭으로 자르면 `UInt32 < 큰 uint64` 가
// 틀리고, 부호가 다른 것을 그대로 비교하면 `-1 < 1u` 가 거짓이다.
#define JBRO_INTEGER_MIXED_RELATIONAL(op, cmp)                                           \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T> || IsIntegerLike<T>::value, int> = 0> \
	constexpr bool operator op(IntegerType<U> lhs, T rhs) noexcept                        \
	{                                                                                    \
		if constexpr (std::is_floating_point_v<T>)                                       \
		{                                                                                \
			return static_cast<T>(lhs.Value) op rhs;                                     \
		}                                                                                \
		else if constexpr (std::is_same_v<T, bool>)                                      \
		{                                                                                \
			return lhs.Value op static_cast<U>(rhs);                                     \
		}                                                                                \
		else                                                                             \
		{                                                                                \
			return std::cmp(lhs.Value, static_cast<typename IntegerRawOf<T>::Type>(rhs)); \
		}                                                                                \
	}                                                                                    \
	template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T> || IsIntegerLike<T>::value, int> = 0> \
	constexpr bool operator op(T lhs, IntegerType<U> rhs) noexcept                        \
	{                                                                                    \
		if constexpr (std::is_floating_point_v<T>)                                       \
		{                                                                                \
			return lhs op static_cast<T>(rhs.Value);                                     \
		}                                                                                \
		else if constexpr (std::is_same_v<T, bool>)                                      \
		{                                                                                \
			return static_cast<U>(lhs) op rhs.Value;                                     \
		}                                                                                \
		else                                                                             \
		{                                                                                \
			return std::cmp(static_cast<typename IntegerRawOf<T>::Type>(lhs), rhs.Value); \
		}                                                                                \
	}

JBRO_INTEGER_MIXED_BINARY(+)
JBRO_INTEGER_MIXED_BINARY(-)
JBRO_INTEGER_MIXED_BINARY(*)
JBRO_INTEGER_MIXED_BINARY(/)
JBRO_INTEGER_MIXED_BINARY(%)
JBRO_INTEGER_MIXED_BINARY(&)
JBRO_INTEGER_MIXED_BINARY(|)
JBRO_INTEGER_MIXED_BINARY(^)

JBRO_INTEGER_MIXED_FLOATING(+)
JBRO_INTEGER_MIXED_FLOATING(-)
JBRO_INTEGER_MIXED_FLOATING(*)
JBRO_INTEGER_MIXED_FLOATING(/)

JBRO_INTEGER_MIXED_RELATIONAL(<, cmp_less)
JBRO_INTEGER_MIXED_RELATIONAL(<=, cmp_less_equal)
JBRO_INTEGER_MIXED_RELATIONAL(>, cmp_greater)
JBRO_INTEGER_MIXED_RELATIONAL(>=, cmp_greater_equal)

template<typename U, typename T, std::enable_if_t<std::is_arithmetic_v<T> || IsIntegerLike<T>::value, int> = 0>
constexpr bool operator==(IntegerType<U> lhs, T rhs) noexcept
{
	if constexpr (std::is_floating_point_v<T>)
	{
		return static_cast<T>(lhs.Value) == rhs;
	}
	else if constexpr (std::is_same_v<T, bool>)
	{
		return lhs.Value == static_cast<U>(rhs);
	}
	else
	{
		return std::cmp_equal(lhs.Value, static_cast<typename IntegerRawOf<T>::Type>(rhs));
	}
}

#undef JBRO_INTEGER_MIXED_BINARY
#undef JBRO_INTEGER_MIXED_RELATIONAL
#undef JBRO_INTEGER_MIXED_FLOATING
}

namespace std
{
	template <typename T>
	struct hash<JBro::IntegerType<T>>
	{
		std::size_t operator()(JBro::IntegerType<T> value) const noexcept
		{
			return hash<T>()(value.Get());
		}
	};
}
