#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/Uuid.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

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

    // **난수 아이디는 버전 4 이고 서로 다르다.** 같은 값이 두 번 나오면 임포트 둘이 한 에셋이 된다.
    void TestGeneratedIdsAreDistinctVersionFour()
    {
        JBro::Table<JBro::Uuid, int> seen;
        for (int index = 0; index < 4096; ++index)
        {
            const JBro::Uuid id = JBro::Uuid::Generate();
            Check(false == id.IsNull(), "a generated id is never null");
            Check(id.GetVersion() == 4, "a generated id carries version 4");
            Check((id.low >> 62) == 2, "and the RFC variant bits");
            Check(seen.Find(id) == nullptr, "no two generated ids collide");
            Check(seen.TryAdd(id, index), "and the table accepts each new id");
        }
    }

    // **이름 아이디는 결정적이고 버전 8 이다.** 빌트인 에셋이 실행마다 같은 아이디여야 캔버스 파일이 그것을
    // 가리킬 수 있고, 난수 아이디와 버전이 달라 겹치지 않는다.
    void TestNamedIdsAreStableAndNeverCollideWithGeneratedOnes()
    {
        const JBro::Uuid cube = JBro::Uuid::FromName("builtin/cube");
        Check(cube == JBro::Uuid::FromName("builtin/cube"), "the same name is the same id");
        Check(cube != JBro::Uuid::FromName("builtin/quad"), "a different name is a different id");
        Check(cube.GetVersion() == 8, "a named id carries version 8");
        Check(JBro::Uuid::Generate().GetVersion() != cube.GetVersion(),
            "so the two families can never produce the same value");
        Check(false == JBro::Uuid::FromName("").IsNull(), "even the empty name is an id, not null");
    }

    // **글자 왕복.** 파일에 적히는 것은 32 자리 소문자 16 진수다. 하이픈 표기도 읽지만 쓰지는 않는다.
    void TestTextRoundTrip()
    {
        JBro::Uuid id;
        id.high = 0x0123456789ABCDEFull;
        id.low = 0xFEDCBA9876543210ull;
        char text[JBro::Uuid::TextCapacity];
        Check(id.ToText(text, sizeof(text)), "a uuid writes into a full buffer");
        Check(std::strcmp(text, "0123456789abcdeffedcba9876543210") == 0, "as 32 lowercase hex digits");
        char small[8];
        Check(false == id.ToText(small, sizeof(small)), "a short buffer is refused");

        JBro::Uuid parsed;
        Check(JBro::Uuid::Parse(text, JBro::Uuid::TextLength, parsed), "the text reads back");
        Check(parsed == id, "to the same value");
        Check(JBro::Uuid::Parse("0123456789ABCDEFFEDCBA9876543210", 32, parsed) && parsed == id,
            "uppercase reads too");
        Check(JBro::Uuid::Parse("01234567-89ab-cdef-fedc-ba9876543210", 36, parsed) && parsed == id,
            "and the hyphenated form");

        JBro::Uuid untouched;
        untouched.high = 7;
        Check(false == JBro::Uuid::Parse("0123456789abcdeffedcba987654321", 31, untouched), "31 digits is refused");
        Check(false == JBro::Uuid::Parse("0123456789abcdeffedcba98765432100", 33, untouched), "33 digits is refused");
        Check(false == JBro::Uuid::Parse("0123456789abcdeffedcba987654321g", 32, untouched), "a non-hex digit is refused");
        Check(false == JBro::Uuid::Parse("0123456789-abcdeffedcba9876543210", 33, untouched),
            "a hyphen off its slot is refused");
        Check(false == JBro::Uuid::Parse("", 0, untouched), "empty is refused");
        Check(untouched.high == 7 && untouched.low == 0, "and a refused parse leaves the value alone");
    }

    // **리플렉션 코덱이 같은 글자를 말한다.** 캔버스 파일과 인스펙터가 이 길로 아이디를 적고 읽는다.
    void TestTheReflectionCodecSpeaksTheSameText()
    {
        const JBro::TypeDescriptor& descriptor = JBro::TypeDescriptorOf<JBro::Uuid>::Get();
        Check(descriptor.codec != nullptr, "the uuid descriptor has a codec");
        Check(descriptor.size == 16, "and describes sixteen bytes");

        const JBro::Uuid id = JBro::Uuid::FromName("codec");
        char buffer[JBro::Uuid::TextCapacity];
        std::size_t required = 0;
        Check(descriptor.codec->ToText(&id, buffer, sizeof(buffer), required), "the codec writes");
        Check(required == JBro::Uuid::TextLength, "and reports the length it needs");
        Check(false == descriptor.codec->ToText(&id, buffer, 4, required) && required == JBro::Uuid::TextLength,
            "a short buffer fails but still reports the length");

        JBro::Uuid read;
        Check(descriptor.codec->FromText(&read, buffer, required), "the codec reads its own text");
        Check(descriptor.codec->Equals(&read, &id), "to an equal value");
        Check(false == descriptor.codec->FromText(&read, "12345", 5), "a bare number is not an id");
        Check(read == id, "and the failed read leaves the value alone");
    }
}

int RunUuidTests()
{
    TestGeneratedIdsAreDistinctVersionFour();
    TestNamedIdsAreStableAndNeverCollideWithGeneratedOnes();
    TestTextRoundTrip();
    TestTheReflectionCodecSpeaksTheSameText();
    std::cout << "Uuid tests passed.\n";
    return 0;
}
