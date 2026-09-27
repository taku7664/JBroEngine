#pragma once

#include <cstdint>
#include <type_traits>

// 비트 묶음 값 타입이다. 생 정수로 들고 다니던 플래그를 담는다.
//
// **왜 필요한가**: 생 정수로 두면 판정을 매번 손으로 적게 된다. `(flags & X) == X` 와
// `(flags & X) != 0` 은 X 가 비트 하나일 때만 같은 뜻이고, 두 비트 이상이면 앞은 "둘 다",
// 뒤는 "하나라도" 다. 손으로 적는 자리마다 이 둘을 골라야 하므로 언젠가 틀린다.
// 그래서 묻는 말을 `HasAll`·`HasAny` 로 갈라 이름에 담는다.
//
// **비트의 뜻은 담지 않는다.** 이 타입은 어떤 비트가 무엇인지 모르므로, 서로 다른 플래그 묶음을
// 섞어 넣어도 잡아 주지 못한다. 묶음마다 타입을 갈라 그것까지 막으려면 플래그 상수를
// `enum class` 로 바꿔야 하는데, 지금 상수들은 직렬화와 스크립트 경계를 함께 건너므로
// 그 작업은 따로 잡았다(D-249 `[열림]`).
//
// 배치는 담은 정수와 같다. DLL 경계를 넘는 구조체에 그대로 넣어도 된다.
namespace JBro
{
    template<typename Underlying>
    class BitFlagT
    {
        static_assert(std::is_unsigned_v<Underlying>,
            "플래그는 부호 없는 정수로 담는다 - 부호 비트가 섞이면 시프트가 구현에 맡겨진다");

    public:
        constexpr BitFlagT() noexcept = default;
        constexpr BitFlagT(Underlying bits) noexcept : Value(bits) {}

        constexpr Underlying Get() const noexcept { return Value; }
        constexpr bool IsEmpty() const noexcept { return Value == Underlying{}; }

        // **넘긴 비트가 전부 켜져 있는가.** 비트 하나를 물을 때도 이것을 쓴다.
        constexpr bool HasAll(Underlying bits) const noexcept
        {
            return (Value & bits) == bits;
        }
        // **넘긴 비트 중 하나라도 켜져 있는가.**
        constexpr bool HasAny(Underlying bits) const noexcept
        {
            return (Value & bits) != Underlying{};
        }
        constexpr bool HasNone(Underlying bits) const noexcept
        {
            return (Value & bits) == Underlying{};
        }

        constexpr void Clear() noexcept { Value = Underlying{}; }
        constexpr void Set(Underlying bits) noexcept { Value = bits; }
        constexpr void Add(Underlying bits) noexcept { Value |= bits; }
        constexpr void Remove(Underlying bits) noexcept { Value &= static_cast<Underlying>(~bits); }
        constexpr void Toggle(Underlying bits) noexcept { Value ^= bits; }
        // 켤지 끌지를 값으로 받는다. 부르는 쪽의 `if` 를 없앤다.
        constexpr void SetTo(Underlying bits, bool on) noexcept
        {
            if (on)
            {
                Add(bits);
            }
            else
            {
                Remove(bits);
            }
        }

        // **정수로는 일부러 암시 변환하지 않는다.** 그러면 `flags & X` 가 조용히 되살아나서
        // 이 타입을 둔 뜻이 없어진다. 정수가 필요하면 `Get()` 이라고 적는다.
        constexpr explicit operator Underlying() const noexcept { return Value; }
        constexpr explicit operator bool() const noexcept { return false == IsEmpty(); }

        constexpr bool operator==(const BitFlagT& rhs) const noexcept { return Value == rhs.Value; }
        constexpr bool operator!=(const BitFlagT& rhs) const noexcept { return Value != rhs.Value; }

        Underlying Value{};
    };

    using BitFlag = BitFlagT<std::uint32_t>;
    using BitFlag64 = BitFlagT<std::uint64_t>;

    static_assert(sizeof(BitFlag) == sizeof(std::uint32_t));
    static_assert(std::is_standard_layout_v<BitFlag>);
    static_assert(std::is_trivially_copyable_v<BitFlag>);
}
