#pragma once

#include <JBro/Types/Float.h>
#include <JBro/Types/IntegerType.h>

#include <type_traits>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Min · Max · Clamp ─ 엔진 값 타입과 원시 값을 섞어 받는 비교 함수
//
//  `std::min(speed, 1.0f)` 는 `speed` 가 `Float` 이면 컴파일되지 않는다 - 템플릿 인자가 `Float` 와
//  `float` 로 갈려 하나로 정해지지 않는다. 필드를 엔진 타입으로 옮기면(D-290) 그런 자리가 엔진 전체에
//  생기므로, 섞여 들어와도 **엔진 타입 쪽으로** 맞춰 돌려주는 함수를 한 번 둔다.
//
//  · 하나라도 `Float` 면 `Float` 다. 정수 강타입이 끼면 그 강타입이다(폭이 다른 둘이면 넓은 쪽).
//  · 둘 다 원시 타입이면 `std::common_type` 이다 - 원시끼리 부르는 자리도 그대로 쓸 수 있다.
//  · 값으로 돌려준다. `std::min` 처럼 참조를 돌려주지 않는다(임시를 가리키는 참조가 남지 않게).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

namespace JBro
{
    namespace Detail
    {
        template<typename T>
        struct ValueRaw
        {
            using Type = T;
        };

        template<>
        struct ValueRaw<Float>
        {
            using Type = float;
        };

        template<typename U>
        struct ValueRaw<IntegerType<U>>
        {
            using Type = U;
        };

        template<typename T>
        inline constexpr bool IsFloatValue = std::is_same_v<std::remove_cvref_t<T>, Float>;

        template<typename T>
        struct IsIntegerValueType : std::false_type
        {
        };

        template<typename U>
        struct IsIntegerValueType<IntegerType<U>> : std::true_type
        {
        };

        template<typename T>
        inline constexpr bool IsIntegerValue = IsIntegerValueType<std::remove_cvref_t<T>>::value;

        template<typename... Ts>
        struct CommonValue
        {
            using Raw = std::common_type_t<typename ValueRaw<std::remove_cvref_t<Ts>>::Type...>;
            using Type = std::conditional_t<(IsFloatValue<Ts> || ...), Float,
                std::conditional_t<(IsIntegerValue<Ts> || ...),
                    std::conditional_t<std::is_integral_v<Raw>, IntegerType<Raw>, Raw>,
                    Raw>>;
        };

        template<typename R, typename T>
        constexpr R ToCommon(const T& value) noexcept
        {
            if constexpr (IsFloatValue<T> || IsIntegerValue<T>)
            {
                return R(static_cast<typename ValueRaw<R>::Type>(value.Get()));
            }
            else
            {
                return R(static_cast<typename ValueRaw<R>::Type>(value));
            }
        }
    }

    template<typename A, typename B>
    constexpr typename Detail::CommonValue<A, B>::Type Min(const A& left, const B& right) noexcept
    {
        using R = typename Detail::CommonValue<A, B>::Type;
        const R a = Detail::ToCommon<R>(left);
        const R b = Detail::ToCommon<R>(right);
        return b < a ? b : a;
    }

    template<typename A, typename B>
    constexpr typename Detail::CommonValue<A, B>::Type Max(const A& left, const B& right) noexcept
    {
        using R = typename Detail::CommonValue<A, B>::Type;
        const R a = Detail::ToCommon<R>(left);
        const R b = Detail::ToCommon<R>(right);
        return a < b ? b : a;
    }

    // `low` 가 `high` 보다 크면 `std::clamp` 처럼 뜻이 없다 - 부르는 쪽이 지킨다.
    template<typename V, typename L, typename H>
    constexpr typename Detail::CommonValue<V, L, H>::Type Clamp(const V& value, const L& low, const H& high) noexcept
    {
        using R = typename Detail::CommonValue<V, L, H>::Type;
        const R v = Detail::ToCommon<R>(value);
        const R lo = Detail::ToCommon<R>(low);
        const R hi = Detail::ToCommon<R>(high);
        return v < lo ? lo : (hi < v ? hi : v);
    }
}
