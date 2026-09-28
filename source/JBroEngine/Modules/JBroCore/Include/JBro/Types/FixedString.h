#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace JBro::Fixed
{
    // 글자를 **제 몸통 안에** 담는 문자열이다(D-257).
    //
    // 이름이 `JBro::String` 과 같고 자리가 다르다. 둘은 성질이 정반대라서 그렇다 -
    // 저쪽은 힙에 담고 길이에 끝이 없으며 POD 가 아니고, 이쪽은 스택에 담고 용량이 정해져 있으며
    // POD 다. `ProjectRule.md` §10.4 가 `JBro::String` 을 POD 구조체·패킷·컴포넌트 공개 필드에
    // 두지 못하게 막는데, **이쪽은 바로 그 자리에 두라고 있는 것이다.**
    //
    // 섞어 쓸 일이 없도록 `JBro` 직속이 아니라 `JBro::Fixed` 아래에 둔다. 두 네임스페이스를
    // `using namespace` 로 함께 열면 이름이 모호해지므로, 게임 스크립트 프렐류드는 `JBro` 만 연다
    // (`JBro::Attribute` 를 따로 둔 것과 같은 까닭이다).
    //
    // **무엇이 좋아지는가.** 지금 코드는 `char buf[N]` 과 `snprintf` 로 같은 일을 쉰 곳 넘게 하는데,
    // 그 가운데 마흔아홉 곳이 반환값을 버린다 - **잘렸는지를 보는 곳이 저장소에 하나도 없다.**
    // 버퍼가 작으면 글자가 조용히 잘리고 아무도 모른다. 이 타입은 잘림을 칸에 적어 두므로
    // 여러 번 이어 붙인 뒤 `IsTruncated()` 를 **한 번만** 물어보면 된다. 매번 확인해야 했던 것이
    // 반환값을 버리게 된 까닭이라, 묻는 횟수를 줄이는 것이 설계의 핵심이다.
    //
    // **쓰는 모양은 잇기다.** 실제 자리를 세어 보니 서식 문자열의 대부분이 `%s` 와 `%u` 이고,
    // 하는 일은 글자를 잇거나 숫자를 글자로 바꾸는 것이었다. 그래서 `Append` 를 타입마다 얹었다.
    //
    //   Fixed::String<64> label;
    //   label.Append(name).Append(" - ").Append(index);
    //   Widget::Text(label.CStr());
    //
    // `N` 은 **담을 수 있는 글자 수**다. 종단 문자는 따로 세지 않으므로 `Fixed::String<8>` 에는
    // 여덟 글자가 들어간다.
    //
    // **UTF-8 은 바이트로만 다룬다.** 잘릴 때 글자 가운데가 끊길 수 있다 - 화면에 내보내는
    // 글자는 잘리지 않을 만큼 용량을 주거나 `IsTruncated()` 를 보고 처리한다.
    template<std::size_t N>
    class String
    {
    public:
        static_assert(N > 0, "a fixed string needs room for at least one character");
        static_assert(N <= 0xFFFFFFFEull, "the length is kept in 32 bits so the type stays small");

        String() = default;

        explicit String(std::string_view text) noexcept
        {
            Append(text);
        }

        // ── 담긴 것 ────────────────────────────────────────────────────────────────────────────

        std::size_t Size() const noexcept
        {
            return static_cast<std::size_t>(m_length);
        }

        static constexpr std::size_t Capacity() noexcept
        {
            return N;
        }

        bool IsEmpty() const noexcept
        {
            return m_length == 0;
        }

        bool IsNotEmpty() const noexcept
        {
            return m_length != 0;
        }

        // **담으려던 것이 용량을 넘었는가.** 이어 붙일 때마다 묻지 않고 다 붙인 뒤 한 번 묻는다.
        // 한 번 참이 되면 `Clear` 전까지 참으로 남는다 - 중간에 넘친 것을 뒤의 성공이 덮지 않는다.
        bool IsTruncated() const noexcept
        {
            return m_truncated;
        }

        // 언제나 종단 문자로 끝난다. 빈 문자열이어도 빈 글자를 가리킨다.
        const char* CStr() const noexcept
        {
            return m_data;
        }

        std::string_view View() const noexcept
        {
            return std::string_view(m_data, m_length);
        }

        // ── 고치기 ─────────────────────────────────────────────────────────────────────────────

        void Clear() noexcept
        {
            m_length = 0;
            m_truncated = false;
            m_data[0] = '\0';
        }

        // 들어간 만큼만 담고, 넘친 것이 있으면 잘림으로 적는다. 잇기가 이어지도록 자기를 돌려준다.
        String& Append(std::string_view text) noexcept
        {
            const std::size_t room = N - static_cast<std::size_t>(m_length);
            std::size_t taken = text.size();
            if (taken > room)
            {
                taken = room;
                m_truncated = true;
            }
            for (std::size_t index = 0; index < taken; ++index)
            {
                m_data[m_length + index] = text[index];
            }
            m_length += static_cast<std::uint32_t>(taken);
            m_data[m_length] = '\0';
            return *this;
        }

        String& Append(char letter) noexcept
        {
            return Append(std::string_view(&letter, 1));
        }

        // 숫자를 글자로 바꿔 잇는다. 서식 문자열의 `%d`·`%u`·`%llu` 자리다.
        String& Append(unsigned long long value) noexcept
        {
            // 뒤에서부터 채우면 다 쓴 뒤 뒤집지 않아도 된다. 64 비트는 스무 자리면 넉넉하다.
            char digits[24] = {};
            std::size_t count = 0;
            do
            {
                digits[sizeof(digits) - 1 - count] = static_cast<char>('0' + (value % 10));
                value /= 10;
                ++count;
            }
            while (value != 0);
            return Append(std::string_view(digits + sizeof(digits) - count, count));
        }

        String& Append(long long value) noexcept
        {
            if (value < 0)
            {
                Append('-');
                // **부호만 떼어 음수의 끝값을 피한다.** `-value` 는 가장 작은 값에서 넘치는데,
                // 부호 있는 정수의 넘침은 정의되지 않은 동작이다. 이 컴파일러에서는 우연히 같은 비트가
                // 나와 시험으로는 가릴 수 없으므로(뮤테이션이 살아남는다), 여기에 까닭을 적어 둔다.
                const unsigned long long magnitude =
                    static_cast<unsigned long long>(-(value + 1)) + 1ull;
                return Append(magnitude);
            }
            return Append(static_cast<unsigned long long>(value));
        }

        String& Append(int value) noexcept
        {
            return Append(static_cast<long long>(value));
        }

        String& Append(unsigned int value) noexcept
        {
            return Append(static_cast<unsigned long long>(value));
        }

        // 소수점 아래 `decimals` 자리까지 쓴다. 자리 수는 0..9 로 자른다.
        String& Append(float value, int decimals = 2) noexcept
        {
            if (decimals < 0)
            {
                decimals = 0;
            }
            else if (decimals > 9)
            {
                decimals = 9;
            }
            char text[64] = {};
            const int written = std::snprintf(text, sizeof(text), "%.*f", decimals,
                static_cast<double>(value));
            if (written <= 0)
            {
                m_truncated = true;
                return *this;
            }
            std::size_t length = static_cast<std::size_t>(written);
            if (length >= sizeof(text))
            {
                length = sizeof(text) - 1;
                m_truncated = true;
            }
            return Append(std::string_view(text, length));
        }

        String& operator+=(std::string_view text) noexcept
        {
            return Append(text);
        }

        String& operator+=(char letter) noexcept
        {
            return Append(letter);
        }

        // 담긴 것을 버리고 새로 담는다. 잘림 표시도 함께 지운다.
        String& Assign(std::string_view text) noexcept
        {
            Clear();
            return Append(text);
        }

        // ── 훑기 ───────────────────────────────────────────────────────────────────────────────
        //
        // 담긴 글자까지만 돈다. 뒤의 빈 자리는 지나지 않는다.

        const char* begin() const noexcept
        {
            return m_data;
        }

        const char* end() const noexcept
        {
            return m_data + m_length;
        }

        char operator[](std::size_t index) const noexcept
        {
            return m_data[index];
        }

    private:
        // **셈하는 칸을 앞에 둔다.** 글자 배열을 앞에 두면 그 뒤에 정렬 패딩이 끼어 타입이 네 바이트
        // 더 커진다. 앞에 두면 배열이 1 바이트 정렬이라 빈틈 없이 붙는다.
        // 길이를 `std::size_t` 가 아니라 32 비트로 드는 것도 같은 까닭이다 - 서른두 글자짜리에
        // 부가 정보가 16 바이트 붙으면 담는 것보다 셈하는 것이 커진다.
        std::uint32_t m_length = 0;
        bool m_truncated = false;
        // 종단 문자 자리를 따로 둔다. `N` 글자를 다 담아도 마지막이 비어 있다.
        char m_data[N + 1] = {};
    };

    // ── 견주기 ─────────────────────────────────────────────────────────────────────────────────
    //
    // **담긴 글자로만 견준다.** 용량이 달라도 같은 글자면 같다 - 용량은 그릇의 크기일 뿐이고,
    // 잘림 표시도 견주지 않는다. 같은 글자를 담았는데 한쪽이 잘려서 왔다면 글자가 다를 것이다.

    template<std::size_t N>
    bool operator==(const String<N>& left, std::string_view right) noexcept
    {
        return left.View() == right;
    }

    template<std::size_t N>
    bool operator!=(const String<N>& left, std::string_view right) noexcept
    {
        return left.View() != right;
    }

    template<std::size_t LeftN, std::size_t RightN>
    bool operator==(const String<LeftN>& left, const String<RightN>& right) noexcept
    {
        return left.View() == right.View();
    }

    template<std::size_t LeftN, std::size_t RightN>
    bool operator!=(const String<LeftN>& left, const String<RightN>& right) noexcept
    {
        return left.View() != right.View();
    }
}
