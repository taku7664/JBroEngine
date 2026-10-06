#pragma once

#include <cstdint>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 크기 값 타입이다. **위치가 아니라 넓이와 높이를 담는다.**
//
// `Vector2` 하나로 위치도 크기도 담으면 둘을 뒤바꿔 넣어도 컴파일러가 잡아 주지 않는다. 각도에서
// 도와 라디안을 섞었던 것과 같은 자리다(D-247). 크기는 뜻이 다른 값이므로 타입도 다르게 둔다 -
// `x`·`y` 가 아니라 `width`·`height` 라고 부르는 것만으로도 읽는 쪽에서 헷갈릴 일이 줄어든다.
//
// 크기는 화면 픽셀(정수)로도 월드 단위(실수)로도 쓰이므로 틀을 하나 두고 별칭을 붙인다.
// **부호 없는 `SizeU` 의 뺄셈은 되감긴다** - 텍스처 크기끼리 빼기 전에 큰 쪽을 확인한다.
//
// 뒤집기·겹치기 같은 사각형 연산은 여기가 아니라 `Math2D.h` 의 `Rect` 에 있다.
namespace JBro
{
    template<typename T>
    struct SizeT
    {
        T width{};
        T height{};

        constexpr SizeT() noexcept = default;
        constexpr SizeT(T w, T h) noexcept : width(w), height(h) {}

        // 넓이나 높이가 0 이면 그리지도 재지도 못한다. 나누기 전에 이것으로 막는다.
        constexpr Bool IsEmpty() const noexcept
        {
            return width <= T{} || height <= T{};
        }

        constexpr T Area() const noexcept
        {
            return width * height;
        }

        // 가로세로비다. **높이가 0 이면 0 을 돌려준다** - 나누어 무한을 만들지 않는다.
        constexpr Float AspectRatio() const noexcept
        {
            if (height == T{})
            {
                return 0.0f;
            }
            return static_cast<float>(width) / static_cast<float>(height);
        }

        template<typename U>
        constexpr SizeT<U> To() const noexcept
        {
            return SizeT<U>(static_cast<U>(width), static_cast<U>(height));
        }

        constexpr SizeT operator+(const SizeT& rhs) const noexcept
        {
            return SizeT(width + rhs.width, height + rhs.height);
        }
        constexpr SizeT operator-(const SizeT& rhs) const noexcept
        {
            return SizeT(width - rhs.width, height - rhs.height);
        }
        constexpr SizeT operator*(T scalar) const noexcept
        {
            return SizeT(width * scalar, height * scalar);
        }
        constexpr SizeT operator/(T scalar) const noexcept
        {
            return SizeT(width / scalar, height / scalar);
        }

        SizeT& operator+=(const SizeT& rhs) noexcept
        {
            width += rhs.width;
            height += rhs.height;
            return *this;
        }
        SizeT& operator-=(const SizeT& rhs) noexcept
        {
            width -= rhs.width;
            height -= rhs.height;
            return *this;
        }
        SizeT& operator*=(T scalar) noexcept
        {
            width *= scalar;
            height *= scalar;
            return *this;
        }
        SizeT& operator/=(T scalar) noexcept
        {
            width /= scalar;
            height /= scalar;
            return *this;
        }

        constexpr bool operator==(const SizeT& rhs) const noexcept
        {
            return width == rhs.width && height == rhs.height;
        }
        constexpr bool operator!=(const SizeT& rhs) const noexcept
        {
            return false == (*this == rhs);
        }
    };

    // 월드·화면의 실수 크기다. 이름이 없는 `Size` 는 이것을 뜻한다.
    using Size = SizeT<Float>;
    // 텍스처·창처럼 픽셀 개수로 세는 크기다.
    using SizeU = SizeT<UInt32>;
    // 음수가 뜻을 갖는 픽셀 크기다(차이·여백).
    using SizeI = SizeT<Int32>;

    // DLL 경계를 넘는 자리에 쓰이므로 배치가 두 스칼라와 같아야 한다(§ POD 규칙).
    static_assert(sizeof(Size) == sizeof(float) * 2);
    static_assert(sizeof(SizeU) == sizeof(std::uint32_t) * 2);
    static_assert(std::is_standard_layout_v<Size>);
    static_assert(std::is_trivially_copyable_v<Size>);
}
