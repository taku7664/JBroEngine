#pragma once

#include <JBro/Types/Float.h>

#include <JBro/Types/StrongTypeOps.h>

#include <cmath>
#include <functional>
#include <type_traits>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Angle ─ 각도의 단위를 타입으로 고정한다(D-247).
//
//  ## 왜 필요한가
//
//  맨 `float` 로 각을 나르면 도인지 라디안인지 이름에만 적힌다. 그 이름이 틀리거나
//  없으면 아무도 못 잡는다. 실제로 이 저장소에는 `Transform2D::rotation`(라디안)과
//  물리 조인트의 `lowerAngle`(도)이 같은 `float` 로 서 있었다.
//
//  ## 무엇을 막고 무엇을 못 막나
//
//  · **막는다**: `Degree` 를 `Radian` 자리에 넘기면 조용히 틀리는 대신 **옳게 변환된다**.
//    두 타입 사이의 변환 생성자가 계수를 곱하기 때문이다.
//  · **못 막는다**: `Radian r = 30.0f;`(사실은 도) 는 그대로 컴파일된다. `float` 와의
//    암시 변환을 열어 두었기 때문이다. 이것을 막으려면 `explicit` 이어야 하는데,
//    그러면 같은 저장소의 `Float`·`Int`·`UInt` 와 어긋나고 `angle = 0.0f` 조차 못 쓴다.
//    지금 얻는 것은 **경계에서의 안전**이고, 리터럴의 단위는 여전히 사람이 맞춘다.
//
//  ## 일부러 없는 것
//
//  각도 × 각도, 각도 ÷ 각도는 뜻이 없어서 만들지 않는다. 스칼라와의 곱·나눗셈만
//  클래스 밖 `JBRO_STRONG_MIXED_*` 로 붙인다. `/` 는 좌항 전용이다 - `angle / 2` 는
//  각도지만 `2 / angle` 은 각도가 아니다.
//
//  ## 리플렉션
//
//  설명서는 이 헤더가 아니라 `JBro/Reflection/CoreTypeDescriptors.h` 에 있다.
//  이 헤더는 매 프레임 경로에 있어 리플렉션 기계를 물고 가면 안 된다(`Color` 와 같다).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace JBro
{
    inline constexpr float Pi = 3.14159265358979323846f;
    inline constexpr float TwoPi = Pi * 2.0f;
    inline constexpr float DegreesToRadians = Pi / 180.0f;
    inline constexpr float RadiansToDegrees = 180.0f / Pi;

    class Radian;

    class Degree
    {
    public:
        constexpr Degree() noexcept = default;
        constexpr Degree(float value) noexcept : Value(value) {}
        constexpr explicit Degree(Float value) noexcept : Value(value.Get()) {}
        constexpr Degree(const Radian& radian) noexcept;

        constexpr operator float() const noexcept { return Value; }
        // `Float` 를 받는 자리로 바로 간다(`Float` ← `float` ← 각도는 사용자 변환이 둘이라 막힌다, D-290).
        constexpr operator Float() const noexcept { return Float(Value); }
        constexpr float Get() const noexcept { return Value; }
        constexpr void Set(float value) noexcept { Value = value; }

        constexpr Radian ToRadian() const noexcept;
        constexpr Degree ToDegree() const noexcept { return *this; }

        // 0 이상 360 미만으로 접는다.
        Degree Normalized360() const
        {
            float value = std::fmod(Value, 360.0f);
            if (value < 0.0f)
            {
                value += 360.0f;
            }
            return Degree(value);
        }

        // -180 초과 180 이하로 접는다. 두 각의 차를 잴 때 쓴다.
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
        // 라디안을 받는 대입이 **따로 있어야 한다.** 없으면 `Degree = Radian` 이
        // `operator=(float)`(Radian -> float) 과 암시 복사 대입(Radian -> Degree) 사이에서
        // 모호해진다. `Radian::operator=(Degree)` 도 같은 이유로 있다.
        Degree& operator=(const Radian& radian) noexcept;
        Degree& operator+=(Degree rhs) noexcept { Value += rhs.Value; return *this; }
        Degree& operator-=(Degree rhs) noexcept { Value -= rhs.Value; return *this; }
        Degree& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
        Degree& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

        friend constexpr Degree operator+(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value + rhs.Value); }
        friend constexpr Degree operator-(Degree lhs, Degree rhs) noexcept { return Degree(lhs.Value - rhs.Value); }
        friend constexpr Degree operator-(Degree value) noexcept { return Degree(-value.Value); }

        friend constexpr bool operator==(Degree lhs, Degree rhs) noexcept { return lhs.Value == rhs.Value; }
        friend constexpr bool operator!=(Degree lhs, Degree rhs) noexcept { return !(lhs == rhs); }
        friend constexpr bool operator<(Degree lhs, Degree rhs) noexcept { return lhs.Value < rhs.Value; }
        friend constexpr bool operator<=(Degree lhs, Degree rhs) noexcept { return lhs.Value <= rhs.Value; }
        friend constexpr bool operator>(Degree lhs, Degree rhs) noexcept { return lhs.Value > rhs.Value; }
        friend constexpr bool operator>=(Degree lhs, Degree rhs) noexcept { return lhs.Value >= rhs.Value; }

        static constexpr Degree FromRadian(float value) noexcept { return Degree(value * RadiansToDegrees); }

        float Value = 0.0f;
    };

    class Radian
    {
    public:
        constexpr Radian() noexcept = default;
        constexpr Radian(float value) noexcept : Value(value) {}
        constexpr explicit Radian(Float value) noexcept : Value(value.Get()) {}
        constexpr Radian(Degree degree) noexcept : Value(degree.Get() * DegreesToRadians) {}

        constexpr operator float() const noexcept { return Value; }
        // `Float` 를 받는 자리로 바로 간다(`Float` ← `float` ← 각도는 사용자 변환이 둘이라 막힌다, D-290).
        constexpr operator Float() const noexcept { return Float(Value); }
        constexpr float Get() const noexcept { return Value; }
        constexpr void Set(float value) noexcept { Value = value; }

        constexpr Degree ToDegree() const noexcept { return Degree(Value * RadiansToDegrees); }
        constexpr Radian ToRadian() const noexcept { return *this; }

        // 0 이상 2π 미만으로 접는다.
        Radian NormalizedTwoPi() const
        {
            float value = std::fmod(Value, TwoPi);
            if (value < 0.0f)
            {
                value += TwoPi;
            }
            return Radian(value);
        }

        // -π 초과 π 이하로 접는다. 두 각의 차를 잴 때 쓴다.
        Radian NormalizedPi() const
        {
            float value = NormalizedTwoPi().Value;
            if (value > Pi)
            {
                value -= TwoPi;
            }
            return Radian(value);
        }

        Radian& operator=(float value) noexcept { Value = value; return *this; }
        Radian& operator=(Degree degree) noexcept { Value = degree.Get() * DegreesToRadians; return *this; }
        Radian& operator+=(Radian rhs) noexcept { Value += rhs.Value; return *this; }
        Radian& operator-=(Radian rhs) noexcept { Value -= rhs.Value; return *this; }
        Radian& operator*=(float rhs) noexcept { Value *= rhs; return *this; }
        Radian& operator/=(float rhs) noexcept { Value /= rhs; return *this; }

        friend constexpr Radian operator+(Radian lhs, Radian rhs) noexcept { return Radian(lhs.Value + rhs.Value); }
        friend constexpr Radian operator-(Radian lhs, Radian rhs) noexcept { return Radian(lhs.Value - rhs.Value); }
        friend constexpr Radian operator-(Radian value) noexcept { return Radian(-value.Value); }

        friend constexpr bool operator==(Radian lhs, Radian rhs) noexcept { return lhs.Value == rhs.Value; }
        friend constexpr bool operator!=(Radian lhs, Radian rhs) noexcept { return !(lhs == rhs); }
        friend constexpr bool operator<(Radian lhs, Radian rhs) noexcept { return lhs.Value < rhs.Value; }
        friend constexpr bool operator<=(Radian lhs, Radian rhs) noexcept { return lhs.Value <= rhs.Value; }
        friend constexpr bool operator>(Radian lhs, Radian rhs) noexcept { return lhs.Value > rhs.Value; }
        friend constexpr bool operator>=(Radian lhs, Radian rhs) noexcept { return lhs.Value >= rhs.Value; }

        static constexpr Radian FromDegree(float value) noexcept { return Radian(value * DegreesToRadians); }

        float Value = 0.0f;
    };

    constexpr Degree::Degree(const Radian& radian) noexcept : Value(radian.Get() * RadiansToDegrees) {}
    constexpr Radian Degree::ToRadian() const noexcept { return Radian(Value * DegreesToRadians); }

    inline Degree& Degree::operator=(const Radian& radian) noexcept
    {
        Value = radian.Get() * RadiansToDegrees;
        return *this;
    }

    // 기본 산술 타입과의 혼합 연산 - 없으면 `angle + 30.0f` 가 모호해서 컴파일되지 않는다.
    // 이유와 규칙은 StrongTypeOps.h 참조.
    JBRO_STRONG_MIXED_BINARY(Degree, float, +)
    JBRO_STRONG_MIXED_BINARY(Degree, float, -)
    JBRO_STRONG_MIXED_BINARY(Degree, float, *)
    JBRO_STRONG_MIXED_BINARY_LHS(Degree, float, /)
    JBRO_STRONG_MIXED_COMPARISONS(Degree, float)

    JBRO_STRONG_MIXED_BINARY(Radian, float, +)
    JBRO_STRONG_MIXED_BINARY(Radian, float, -)
    JBRO_STRONG_MIXED_BINARY(Radian, float, *)
    JBRO_STRONG_MIXED_BINARY_LHS(Radian, float, /)
    JBRO_STRONG_MIXED_COMPARISONS(Radian, float)

    // 저장 파일과 GPU 버퍼가 `float` 자리에 그대로 놓는다. 리플렉션 설명서도 이 보증에
    // 기대어 `float` 코덱을 쓴다 - 첫 멤버의 주소가 곧 객체의 주소여야 한다.
    static_assert(sizeof(Degree) == sizeof(float));
    static_assert(alignof(Degree) == alignof(float));
    static_assert(std::is_trivially_copyable_v<Degree>);
    static_assert(std::is_standard_layout_v<Degree>);

    static_assert(sizeof(Radian) == sizeof(float));
    static_assert(alignof(Radian) == alignof(float));
    static_assert(std::is_trivially_copyable_v<Radian>);
    static_assert(std::is_standard_layout_v<Radian>);
}

namespace std
{
    template <>
    struct hash<JBro::Degree>
    {
        std::size_t operator()(JBro::Degree value) const noexcept
        {
            return hash<float>()(value.Get());
        }
    };

    template <>
    struct hash<JBro::Radian>
    {
        std::size_t operator()(JBro::Radian value) const noexcept
        {
            return hash<float>()(value.Get());
        }
    };
}
