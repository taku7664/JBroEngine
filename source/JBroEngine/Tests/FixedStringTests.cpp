#include <JBro/Types/FixedString.h>
#include <JBro/Types/String.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // **빈 것도 글자로 쓸 수 있어야 한다.** `CStr()` 이 널을 돌려주면 받는 쪽이 모두 검사해야 한다.
    void TestEmptyIsUsableAsText()
    {
        JBro::Fixed::String<16> text;
        Check(text.IsEmpty(), "a fresh fixed string is empty");
        Check(false == text.IsNotEmpty(), "and not non-empty");
        Check(text.Size() == 0, "its size is zero");
        Check(text.Capacity() == 16, "its capacity is what was asked for");
        Check(text.CStr() != nullptr, "but it still hands out a usable pointer");
        Check(text.CStr()[0] == '\0', "pointing at a terminator");
        Check(text.View().empty(), "and its view is empty");
        Check(false == text.IsTruncated(), "nothing was dropped yet");
    }

    // **잇기가 이어진다.** 서식 문자열 자리를 대신하려면 한 줄로 죽 이어 쓸 수 있어야 한다.
    void TestAppendChains()
    {
        JBro::Fixed::String<64> label;
        label.Append("Layer").Append(' ').Append(3).Append(" - ").Append("ground");
        Check(label == "Layer 3 - ground", "appends chain in order");
        Check(label.Size() == 16, "and the size counts every byte written");
        Check(std::strcmp(label.CStr(), "Layer 3 - ground") == 0, "the C string matches too");
        Check(false == label.IsTruncated(), "nothing was dropped");
    }

    // **넘치면 자르고 그 사실을 적어 둔다.** 지금 코드가 `snprintf` 반환값을 버려서 놓치던 자리다.
    void TestOverflowTruncatesAndSaysSo()
    {
        JBro::Fixed::String<8> small;
        small.Append("abcdefghij");
        Check(small.Size() == 8, "it keeps exactly what fits");
        Check(small == "abcdefgh", "and keeps the front, dropping the tail");
        Check(small.IsTruncated(), "and it says that something was dropped");
        Check(small.CStr()[8] == '\0', "the buffer is still terminated at the brim");
    }

    // **딱 맞게 채워도 잘림이 아니다.** 경계에서 한 칸을 잘못 세면 여기가 어긋난다.
    void TestFillingToTheBrimIsNotTruncation()
    {
        JBro::Fixed::String<4> exact;
        exact.Append("abcd");
        Check(exact.Size() == 4, "four characters fit in a string of four");
        Check(exact == "abcd", "and all of them are kept");
        Check(false == exact.IsTruncated(), "filling it exactly is not truncation");

        exact.Append("e");
        Check(exact.Size() == 4, "one more does not fit");
        Check(exact.IsTruncated(), "and that is truncation");
    }

    // **한 번 잘리면 그 뒤의 성공이 덮지 않는다.** 여러 번 이어 붙인 뒤 한 번만 묻는 것이
    // 이 타입의 요점이므로, 중간에 잃은 것이 끝에서 보여야 한다.
    void TestTruncationSticksUntilCleared()
    {
        JBro::Fixed::String<4> text;
        text.Append("abcdef");
        Check(text.IsTruncated(), "the long piece was cut");

        text.Clear();
        Check(false == text.IsTruncated(), "clearing forgets it");
        Check(text.IsEmpty(), "and empties the text");

        text.Append("ab");
        text.Append("cdef");
        text.Append("");
        Check(text.IsTruncated(), "a later successful append does not hide the earlier loss");

        text.Assign("xy");
        Check(false == text.IsTruncated(), "assigning starts over, flag and all");
        Check(text == "xy", "with only the new text");
    }

    // **비우고 짧게 다시 담으면 앞의 꼬리가 남지 않는다.** 버퍼는 그대로 두고 길이만 되돌리므로,
    // 종단 문자를 제자리에 쓰지 않으면 `CStr()` 이 지난번의 긴 글자를 마저 읽는다.
    // 길이와 `View()` 만 보는 시험은 이것을 놓친다 - `CStr()` 로 확인해야 한다.
    void TestReusingTheBufferLeavesNoTail()
    {
        JBro::Fixed::String<32> text;
        text.Append("a long sentence");
        Check(std::strcmp(text.CStr(), "a long sentence") == 0, "the long text is there first");

        text.Clear();
        text.Append("ab");
        Check(text.Size() == 2, "the new text is two characters");
        Check(std::strcmp(text.CStr(), "ab") == 0, "and the C string stops there, with no tail from before");
        Check(std::strlen(text.CStr()) == 2, "so its length as a C string matches its size");

        text.Assign("xyz");
        Check(std::strcmp(text.CStr(), "xyz") == 0, "assigning over a longer text leaves no tail either");

        // 한 글자씩 담을 때도 매번 종단이 제자리에 있어야 한다.
        JBro::Fixed::String<8> letters;
        letters.Append('h');
        Check(std::strcmp(letters.CStr(), "h") == 0, "one character is terminated");
        letters.Append('i');
        Check(std::strcmp(letters.CStr(), "hi") == 0, "and so are two");
    }

    // **숫자를 글자로 바꾼다.** 서식 문자열의 `%d`·`%u`·`%llu` 자리를 대신한다.
    void TestNumbers()
    {
        JBro::Fixed::String<32> text;
        text.Append(0);
        Check(text == "0", "zero is one digit, not empty");

        text.Assign("").Append(1234567890);
        Check(text == "1234567890", "a large int keeps every digit");

        text.Assign("").Append(-42);
        Check(text == "-42", "a negative int carries its sign");

        text.Assign("").Append(static_cast<unsigned int>(4000000000u));
        Check(text == "4000000000", "an unsigned int does not wrap into a negative");

        text.Assign("").Append(18446744073709551615ull);
        Check(text == "18446744073709551615", "the largest 64-bit value fits");

        // **가장 작은 정수가 함정이다.** 부호를 뒤집으면 넘쳐서 같은 음수로 돌아온다.
        text.Assign("").Append(static_cast<long long>(-9223372036854775807LL - 1));
        Check(text == "-9223372036854775808", "the smallest 64-bit value does not overflow while negating");
    }

    void TestFloats()
    {
        JBro::Fixed::String<32> text;
        text.Append(1.5f);
        Check(text == "1.50", "a float takes two decimals by default");

        text.Assign("").Append(1.5f, 0);
        Check(text == "2", "zero decimals rounds to a whole number");

        text.Assign("").Append(-0.125f, 3);
        Check(text == "-0.125", "and a negative keeps its sign and digits");

        text.Assign("").Append(3.14159f, 20);
        Check(text.IsNotEmpty(), "asking for absurd precision still writes something");
    }

    // **담긴 글자로만 견준다.** 용량이 다른 두 문자열이 같은 글자를 담으면 같다.
    void TestComparisonLooksOnlyAtTheText()
    {
        JBro::Fixed::String<8> small;
        JBro::Fixed::String<64> large;
        small.Append("hello");
        large.Append("hello");
        Check(small == large, "same text in different capacities compares equal");
        Check(false == (small != large), "and not unequal");

        large.Append("!");
        Check(small != large, "different text compares unequal");
        Check(false == (small == large), "and equality says so as well");

        // **앞글자가 같아도 뒤가 다르면 다르다.** 첫 바이트만 견주는 구현은 여기서 드러난다.
        JBro::Fixed::String<16> hello;
        JBro::Fixed::String<16> help;
        hello.Append("hello");
        help.Append("help");
        Check(false == (hello == help), "sharing a prefix is not being equal");
        Check(hello != help, "they are different");

        JBro::Fixed::String<16> longer;
        longer.Append("hellohello");
        Check(false == (hello == longer), "nor is being a prefix of the other");

        Check(small == std::string_view("hello"), "it compares against a view");
        Check(small != std::string_view("world"), "and tells different text apart");

        // 잘림 표시는 견주지 않는다 - 글자가 같으면 같다.
        JBro::Fixed::String<5> cut;
        cut.Append("hello world");
        Check(cut.IsTruncated(), "this one lost its tail");
        Check(cut == small, "but what is left is the same text, so they are equal");
    }

    // **담긴 데까지만 훑는다.** 뒤의 빈 자리를 함께 돌면 글자 수가 용량만큼으로 보인다.
    void TestIterationStopsAtTheText()
    {
        JBro::Fixed::String<32> text;
        text.Append("abc");

        int count = 0;
        int sum = 0;
        for (const char letter : text)
        {
            ++count;
            sum += letter;
        }
        Check(count == 3, "iteration walks only the characters that are there");
        Check(sum == 'a' + 'b' + 'c', "and hands them over in order");
        Check(text[0] == 'a' && text[2] == 'c', "indexing reaches them too");

        JBro::Fixed::String<8> empty;
        int emptyCount = 0;
        for (const char letter : empty)
        {
            static_cast<void>(letter);
            ++emptyCount;
        }
        Check(emptyCount == 0, "an empty one walks nothing");
    }

    // **경계를 넘을 수 있는 값이다.** POD 여야 컴포넌트 칸과 패킷에 둘 수 있고, 그것이
    // `JBro::String` 을 두지 못하는 자리를 메우는 이 타입의 존재 이유다.
    void TestItIsPlainDataUnlikeTheHeapString()
    {
        using Text = JBro::Fixed::String<32>;
        static_assert(std::is_trivially_copyable_v<Text>, "a fixed string must cross the DLL boundary by value");
        static_assert(std::is_standard_layout_v<Text>, "and keep a predictable layout");
        static_assert(false == std::is_trivially_copyable_v<JBro::String>,
            "the heap string is not, which is exactly why this type exists");

        Text source;
        source.Append("carry me");

        unsigned char buffer[sizeof(Text)] = {};
        std::memcpy(buffer, &source, sizeof(Text));
        Text copied;
        std::memcpy(&copied, buffer, sizeof(Text));

        Check(copied == source, "a byte copy is the same text");
        Check(copied.Size() == source.Size(), "with the same size");
        Check(std::strcmp(copied.CStr(), "carry me") == 0, "and it still reads as a C string");
    }

    // **힙 문자열과 오간다.** 두 타입이 같은 저장소에 함께 있을 것이므로 넘기는 길이 있어야 한다.
    void TestItInteroperatesWithTheHeapString()
    {
        const JBro::String heap = "from the heap";
        JBro::Fixed::String<32> fixed;
        fixed.Append(heap.View());
        Check(fixed == "from the heap", "a heap string feeds in through its view");

        const JBro::String back(fixed.View());
        Check(back == "from the heap", "and the fixed one feeds back the same way");
        Check(back.size() == fixed.Size(), "with the same length");
    }

    // **같은 이름의 두 타입이 서로 다른 것으로 남는다.** 네임스페이스로만 갈라 두었으므로,
    // 한쪽을 다른 쪽인 줄 알고 쓰는 일이 없는지 타입으로 짚어 둔다.
    void TestTheTwoStringsAreDistinctTypes()
    {
        static_assert(false == std::is_same_v<JBro::String, JBro::Fixed::String<32>>,
            "the two Strings are different types that merely share a name");
        static_assert(false == std::is_same_v<JBro::Fixed::String<8>, JBro::Fixed::String<16>>,
            "and two capacities are different types as well");

        JBro::Fixed::String<8> eight;
        JBro::Fixed::String<16> sixteen;
        eight.Append("same");
        sixteen.Append("same");
        Check(eight == sixteen, "different types still compare by their text");
    }

    // **크기가 예측된다.** 컴포넌트 칸에 두려면 얼마를 차지하는지 알 수 있어야 한다.
    void TestSizeIsPredictable()
    {
        // 글자 N 개 + 종단 하나 + 길이(4) + 잘림 표시(1). 정렬 때문에 딱 떨어지지는 않으므로
        // 넘지 않아야 할 선만 짚는다 - 셈하는 몫이 담는 몫을 밀어내지 않는지 보는 것이다.
        static_assert(sizeof(JBro::Fixed::String<32>) <= 32 + 1 + 8,
            "a fixed string carries its text plus a little bookkeeping, nothing more");
        static_assert(sizeof(JBro::Fixed::String<256>) <= 256 + 1 + 8,
            "and the bookkeeping does not grow with the capacity");
        Check(JBro::Fixed::String<32>::Capacity() == 32, "capacity is known without an instance");

        std::cout << "  [measure] Fixed::String<32> is " << sizeof(JBro::Fixed::String<32>)
            << " bytes, <256> is " << sizeof(JBro::Fixed::String<256>) << '\n';
    }
}

int RunFixedStringTests()
{
    try
    {
        TestEmptyIsUsableAsText();
        TestAppendChains();
        TestOverflowTruncatesAndSaysSo();
        TestFillingToTheBrimIsNotTruncation();
        TestTruncationSticksUntilCleared();
        TestReusingTheBufferLeavesNoTail();
        TestNumbers();
        TestFloats();
        TestComparisonLooksOnlyAtTheText();
        TestIterationStopsAtTheText();
        TestItIsPlainDataUnlikeTheHeapString();
        TestItInteroperatesWithTheHeapString();
        TestTheTwoStringsAreDistinctTypes();
        TestSizeIsPredictable();
    }
    catch (const std::exception&)
    {
        return 1;
    }

    std::cout << "fixed string tests passed\n";
    return 0;
}
