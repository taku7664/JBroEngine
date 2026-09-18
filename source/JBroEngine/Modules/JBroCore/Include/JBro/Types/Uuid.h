#pragma once

#include <JBro/Types/Hash.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 128 비트 범용 고유 식별자다. **정수 둘이 전부이고 글자는 파일에 적을 때만 만든다.**
    //
    // 영속 식별자(에셋 아이디 등)는 이 타입의 별칭이다. 기존 엔진은 GUID 를 `fs::path` 를 상속한
    // 문자열로 들고 뒤에 `Guid128 { Hi, Lo }` 를 덧붙여 같은 아이디가 두 벌이 됐고, 조회마다 변환이
    // 붙었다(D-111). 여기서는 메모리와 표의 키가 언제나 이 두 정수다.
    //
    // 비트 배치는 RFC 9562 를 따른다. `high` 의 비트 12..15 가 버전, `low` 의 상위 두 비트가 변형이다.
    // `Generate` 는 버전 4(난수), `FromName` 은 버전 8(사용자 정의 - 이름 해시)이다. 둘의 버전이 달라
    // 빌트인 이름에서 만든 아이디와 임포트 때 뽑은 아이디가 겹치지 않는다.
    struct Uuid
    {
        std::uint64_t high = 0;
        std::uint64_t low = 0;

        // 32 자리 16 진수(하이픈 없음)와 그 뒤의 NUL 을 담는 길이다.
        static constexpr std::size_t TextLength = 32;
        static constexpr std::size_t TextCapacity = TextLength + 1;

        constexpr bool IsNull() const noexcept
        {
            return high == 0 && low == 0;
        }

        constexpr std::uint8_t GetVersion() const noexcept
        {
            return static_cast<std::uint8_t>((high >> 12) & 0xF);
        }

        friend constexpr bool operator==(const Uuid& left, const Uuid& right) noexcept
        {
            return left.high == right.high && left.low == right.low;
        }

        friend constexpr bool operator!=(const Uuid& left, const Uuid& right) noexcept
        {
            return false == (left == right);
        }

        // 난수로 새 아이디를 만든다(버전 4). 프레임 경로가 아니라 임포트·생성 시점에만 부른다.
        static Uuid Generate();

        // 이름에서 결정적으로 만든다(버전 8). 같은 이름은 언제나 같은 아이디다 - 빌트인 에셋이 쓴다.
        // `Generate` 와 버전이 다르므로 난수 아이디와 겹치지 않는다.
        static Uuid FromName(const char* name) noexcept;

        // 32 자리 소문자 16 진수로 적는다. `buffer` 는 `TextCapacity` 이상이어야 하고 NUL 로 끝낸다.
        // 모자라면 false 다.
        bool ToText(char* buffer, std::size_t capacity) const noexcept;

        // 32 자리 16 진수(대소문자 무관)를 읽는다. 8-4-4-4-12 하이픈 표기도 받는다. 그 외는 false 이고
        // `result` 를 건드리지 않는다.
        static bool Parse(const char* text, std::size_t length, Uuid& result) noexcept;
    };

    static_assert(sizeof(Uuid) == 16, "a uuid is exactly two 64-bit words");

    template<>
    struct Hash<Uuid>
    {
        using IsTransparent = void;

        std::size_t operator()(const Uuid& value) const noexcept
        {
            std::size_t seed = static_cast<std::size_t>(value.high);
            HashCombine(seed, static_cast<std::size_t>(value.low));
            return seed;
        }
    };
}
