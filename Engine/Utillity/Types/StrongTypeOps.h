#pragma once

#include <type_traits>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  StrongTypeOps ─ 강타입(Float/Int/UInt/Degree/Radian)과 기본 산술 타입의 혼합 연산
//
//  ## 왜 필요한가
//
//  강타입들은 기본 타입과의 왕래를 편하게 하려고 **양방향 암시 변환**을 갖고 있다:
//    · `Float(float)`      — 암시 ctor      (`Float speed = 1.5f;`)
//    · `operator float()`  — 암시 변환 연산자 (`takesFloat(speed);`)
//
//  이 둘이 같이 있으면 `speed * dt`(Float × float) 가 **모호해서 컴파일되지 않는다**(C2666):
//    · `operator*(Float, Float)` — 좌항 정확 일치, 우항 float→Float 사용자 변환
//    · 기본 제공 `float * float` — 좌항 Float→float 사용자 변환, 우항 정확 일치
//  각자 한쪽씩 이겨서 어느 쪽도 더 낫지 않다 → 모호. 게임 스크립트에서 제일 흔한
//  `Speed * Time->GetDeltaSeconds()` 한 줄이 이것 때문에 안 된다.
//
//  ## 해법
//
//  아래 매크로가 만드는 오버로드는 **양쪽 인자가 모두 정확 일치**라 위 두 후보를 다 이긴다.
//  T 를 템플릿으로 열어 둔 건 리터럴 때문이다 — `dt * 2`(int), `dt * 0.5`(double) 까지
//  한 벌로 덮는다. 강타입끼리는 T 가 is_arithmetic 이 아니라 여기 걸리지 않으므로
//  기존 `operator*(Float, Float)` 가 그대로 이긴다(강타입 보존).
//
//  ## 사용법 — 클래스 정의 **뒤**에 붙인다(Value 가 public 이어야 한다)
//
//    JBRO_STRONG_MIXED_BINARY(Float, float, +)
//    JBRO_STRONG_MIXED_RELATIONAL(Float, float, <)
//    JBRO_STRONG_MIXED_EQUALITY(Float, float)
//
//  ## 주의
//
//  · 관계 연산자(`< <= > >=`)는 **양방향을 다 만든다**. C++20 이 역순 후보를 합성해 주는 건
//    `==`/`!=` 뿐이라(`<` 류는 `<=>` 가 있어야 한다), 한쪽만 있으면 `0.0f < speed` 가 모호하다.
//  · `==` 는 **한 방향만** 만든다. C++20 이 역순(`0.0f == speed`)과 `!=` 를 합성하므로,
//    역순을 손으로 또 적으면 합성 후보와 맞비겨 도로 모호해진다.
//  · 이미 같은 짝의 비템플릿 오버로드가 있어도 안전하다(예: Degree 의 `operator*(Degree, float)`).
//    정확 일치가 비기면 비템플릿이 이기고, 리터럴처럼 변환이 끼면 이쪽이 이긴다 — 둘 다 모호하지 않다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 산술 이항 연산 — 결과는 강타입으로 유지된다.
#define JBRO_STRONG_MIXED_BINARY(StrongType, Underlying, op)                       \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr StrongType operator op(StrongType lhs, T rhs) noexcept               \
	{                                                                              \
		return StrongType(lhs.Value op static_cast<Underlying>(rhs));              \
	}                                                                              \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr StrongType operator op(T lhs, StrongType rhs) noexcept               \
	{                                                                              \
		return StrongType(static_cast<Underlying>(lhs) op rhs.Value);              \
	}

// 좌항이 강타입일 때만 만드는 이항 연산 — 역순이 뜻을 잃는 연산에 쓴다.
// 예: `angle / 2` 는 각도지만 `2 / angle` 은 각도가 아니다.
#define JBRO_STRONG_MIXED_BINARY_LHS(StrongType, Underlying, op)                   \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr StrongType operator op(StrongType lhs, T rhs) noexcept               \
	{                                                                              \
		return StrongType(lhs.Value op static_cast<Underlying>(rhs));              \
	}

// 관계 연산 — 양방향.
#define JBRO_STRONG_MIXED_RELATIONAL(StrongType, Underlying, op)                   \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr bool operator op(StrongType lhs, T rhs) noexcept                     \
	{                                                                              \
		return lhs.Value op static_cast<Underlying>(rhs);                          \
	}                                                                              \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr bool operator op(T lhs, StrongType rhs) noexcept                     \
	{                                                                              \
		return static_cast<Underlying>(lhs) op rhs.Value;                          \
	}

// 동등 비교 — 한 방향만(역순과 `!=` 는 C++20 이 합성한다).
#define JBRO_STRONG_MIXED_EQUALITY(StrongType, Underlying)                         \
	template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>       \
	constexpr bool operator==(StrongType lhs, T rhs) noexcept                      \
	{                                                                              \
		return lhs.Value == static_cast<Underlying>(rhs);                          \
	}

// 관계 4종 + 동등 — 비교는 늘 한 묶음으로 필요하다.
#define JBRO_STRONG_MIXED_COMPARISONS(StrongType, Underlying)                      \
	JBRO_STRONG_MIXED_RELATIONAL(StrongType, Underlying, <)                        \
	JBRO_STRONG_MIXED_RELATIONAL(StrongType, Underlying, <=)                       \
	JBRO_STRONG_MIXED_RELATIONAL(StrongType, Underlying, >)                        \
	JBRO_STRONG_MIXED_RELATIONAL(StrongType, Underlying, >=)                       \
	JBRO_STRONG_MIXED_EQUALITY(StrongType, Underlying)
