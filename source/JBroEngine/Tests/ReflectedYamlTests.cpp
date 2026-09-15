#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

// 리플렉션이 설명하는 값을 **타입을 모른 채** YAML 로 쓰고 읽는다(D-86).
//
// 캔버스 파일·에디터 스냅샷·되돌리기가 모두 이 걸음을 쓴다. 여기서 재는 것은
// 컨테이너다 - 처음에는 캔버스 파일이 컨테이너를 거절하고 스냅샷은 조용히 건너뛰었다.

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // 맵으로 적히는 구조체다. 한 필드는 저장에서 빠진다 - 원소 안에서도 그래야 한다.
    struct Pair
    {
        float kept = 0.0f;
        float dropped = 0.0f;
    };

    using FloatList = JBro::Array<float>;
    using Rows = JBro::Array<FloatList>;
    using Counts = JBro::Table<JBro::String, std::int32_t>;
    using Named = JBro::Table<JBro::String, FloatList>;
}

namespace JBro
{
    template <>
    struct TypeDescriptorOf<Pair>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Pair::kept>(),
                MakeFieldEntry<&Pair::dropped>(Attribute::NoSerialize()),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Pair>("Test::Pair", fields.Get());
            return descriptor;
        }
    };
}

namespace
{
    bool Near(float value, float expected)
    {
        const float delta = value - expected;
        return delta > -0.0001f && delta < 0.0001f;
    }

    JBro::String WriteAs(const JBro::TypeDescriptor& type, const void* value)
    {
        JBro::YamlWriter writer;
        JBro::ReflectedYamlError error;
        if (false == JBro::WriteReflectedValue(writer, "Value", type, value, error))
        {
            std::cout << "  write failed: " << error.message.c_str() << std::endl;
            Check(false, "the value must write");
        }
        return writer.GetText();
    }

    bool ReadAs(const JBro::String& text, const JBro::TypeDescriptor& type, void* value,
        JBro::ReflectedYamlError& error)
    {
        JBro::YamlDocument document;
        JBro::YamlError parseError;
        if (false == document.Parse(text.c_str(), text.size(), parseError))
        {
            std::cout << "written:\n" << text.c_str() << "  parse failed at line "
                << parseError.line << ": " << parseError.message.c_str() << std::endl;
            Check(false, "what was written must parse");
        }
        const std::uint32_t node = document.Find(document.GetRoot(), "Value");
        Check(node != JBro::YamlDocument::InvalidNode, "the value must be where it was written");
        return JBro::ReadReflectedValue(document, node, type, value, error);
    }

    void ReadBack(const JBro::String& text, const JBro::TypeDescriptor& type, void* value)
    {
        JBro::ReflectedYamlError error;
        if (false == ReadAs(text, type, value, error))
        {
            std::cout << "written:\n" << text.c_str() << "  read failed: "
                << error.message.c_str() << std::endl;
            Check(false, "what was written must read back");
        }
    }

    // 숫자 배열이 갔다가 돌아온다. **읽기는 있던 원소를 버리고 파일의 것으로 채운다** -
    // 되돌리기가 원소 수까지 되살려야 하기 때문이다.
    void TestAnArrayOfNumbersGoesThereAndBack()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<FloatList>::Get();
        FloatList written;
        written.Add(1.5f);
        written.Add(-2.0f);
        written.Add(0.25f);
        const JBro::String text = WriteAs(type, &written);

        FloatList read;
        for (int index = 0; index < 9; ++index)
        {
            read.Add(99.0f);
        }
        ReadBack(text, type, &read);
        Check(read.Size() == 3, "reading must leave exactly the written elements");
        Check(Near(read[0], 1.5f) && Near(read[1], -2.0f) && Near(read[2], 0.25f),
            "in the written order");
    }

    void TestAnEmptyArrayStaysEmpty()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<FloatList>::Get();
        FloatList written;
        const JBro::String text = WriteAs(type, &written);

        FloatList read;
        read.Add(7.0f);
        ReadBack(text, type, &read);
        Check(read.IsEmpty(), "an empty array must read back empty, clearing what was there");
    }

    // **나열로 적는 원소는 대시만 있는 줄 아래에 적는다.** 기존 엔진 `.jcanvas` 의
    // `LocalPoints` 가 이 모양이다. 모양이 틀리면 한 줄에 `- - 1` 이 되어 파서가
    // 글자 하나로 읽는다.
    void TestPackedElementsAreWrittenTheLegacyWay()
    {
        using Colors = JBro::Array<JBro::Color>;
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Colors>::Get();
        Colors written;
        written.Add(JBro::Color{1.0f, 0.5f, 0.25f, 1.0f});
        written.Add(JBro::Color{0.0f, 0.75f, 1.0f, 0.5f});
        const JBro::String text = WriteAs(type, &written);

        const char* const expected =
            "Value:\n"
            "  -\n"
            "    - 1\n"
            "    - 0.5\n"
            "    - 0.25\n"
            "    - 1\n"
            "  -\n"
            "    - 0\n"
            "    - 0.75\n"
            "    - 1\n"
            "    - 0.5\n";
        if (text != expected)
        {
            std::cout << "written:\n" << text.c_str();
        }
        Check(text == expected, "packed elements must sit under a bare dash");

        Colors read;
        ReadBack(text, type, &read);
        Check(read.Size() == 2, "both colors must come back");
        Check(Near(read[0].G, 0.5f) && Near(read[1].B, 1.0f) && Near(read[1].A, 0.5f),
            "with their members in place");
    }

    // 배열의 배열. 안쪽이 빈 것도 섞는다 - 빈 안쪽은 `[]` 로 적혀야 다음 원소와 갈린다.
    void TestArraysOfArraysKeepTheirShape()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Rows>::Get();
        Rows written;
        written.Add(FloatList{});
        written[0].Add(1.0f);
        written[0].Add(2.0f);
        written.Add(FloatList{});
        written.Add(FloatList{});
        written[2].Add(3.0f);
        const JBro::String text = WriteAs(type, &written);

        Rows read;
        ReadBack(text, type, &read);
        Check(read.Size() == 3, "three rows must come back");
        Check(read[0].Size() == 2 && Near(read[0][1], 2.0f), "the first with two");
        Check(read[1].IsEmpty(), "the second empty");
        Check(read[2].Size() == 1 && Near(read[2][0], 3.0f), "the third with one");
    }

    // 구조체 원소는 맵으로 적히고, 저장하지 않는 필드는 원소 안에서도 빠진다.
    void TestStructElementsAreWrittenAsMaps()
    {
        using Pairs = JBro::Array<Pair>;
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Pairs>::Get();
        Pairs written;
        written.Add(Pair{1.5f, 8.0f});
        written.Add(Pair{2.5f, 9.0f});
        const JBro::String text = WriteAs(type, &written);
        Check(std::strstr(text.c_str(), "dropped") == nullptr,
            "a field marked not to be saved must not be written inside an element either");

        Pairs read;
        ReadBack(text, type, &read);
        Check(read.Size() == 2, "both elements must come back");
        Check(Near(read[0].kept, 1.5f) && Near(read[1].kept, 2.5f), "with their saved field");
        Check(Near(read[0].dropped, 0.0f), "and the unsaved one left at its default");
    }

    // **표는 키 글자 순으로 적는다.** 슬롯 순서는 넣은 내력에 따라 달라지므로, 그대로
    // 적으면 내용이 같은 두 표가 다른 글자가 된다 - 되돌리기가 "바뀌었다" 고 잘못 본다.
    void TestATableIsWrittenInKeyOrder()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Counts>::Get();
        Counts written;
        written.TryAdd(JBro::String("zeta"), 1);
        written.TryAdd(JBro::String("alpha"), 2);
        written.TryAdd(JBro::String("mid"), 3);
        const JBro::String text = WriteAs(type, &written);

        const char* const alpha = std::strstr(text.c_str(), "alpha");
        const char* const mid = std::strstr(text.c_str(), "mid");
        const char* const zeta = std::strstr(text.c_str(), "zeta");
        Check(alpha != nullptr && mid != nullptr && zeta != nullptr, "every key must be written");
        Check(alpha < mid && mid < zeta, "in key order, not slot order");

        Counts reversed;
        reversed.TryAdd(JBro::String("mid"), 3);
        reversed.TryAdd(JBro::String("alpha"), 2);
        reversed.TryAdd(JBro::String("zeta"), 1);
        Check(WriteAs(type, &reversed) == text,
            "the same contents added in another order must write the same text");

        Counts read;
        read.TryAdd(JBro::String("stale"), 42);
        ReadBack(text, type, &read);
        Check(read.Size() == 3, "reading must leave exactly the written entries");
        Check(read.Find(JBro::String("stale")) == nullptr, "dropping what was there");
        const std::int32_t* found = read.Find(JBro::String("alpha"));
        Check(found != nullptr && *found == 2, "and carry each value with its key");
    }

    // 표의 값이 배열이어도 같은 걸음이다.
    void TestATableOfArraysGoesThereAndBack()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Named>::Get();
        Named written;
        FloatList first;
        first.Add(4.0f);
        first.Add(5.0f);
        written.TryAdd(JBro::String("first"), first);
        written.TryAdd(JBro::String("empty"), FloatList{});
        const JBro::String text = WriteAs(type, &written);

        Named read;
        ReadBack(text, type, &read);
        Check(read.Size() == 2, "both entries must come back");
        const FloatList* found = read.Find(JBro::String("first"));
        Check(found != nullptr && found->Size() == 2 && Near((*found)[1], 5.0f),
            "with the list under its key");
        const FloatList* empty = read.Find(JBro::String("empty"));
        Check(empty != nullptr && empty->IsEmpty(), "and the empty one empty");
    }

    // **같은 키가 두 번 나오면 실패다.** 뒤의 것으로 덮으면 앞의 값이 조용히 사라진다.
    void TestATableNamingAKeyTwiceIsRefused()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Counts>::Get();
        const JBro::String text(
            "Value:\n"
            "  - Key: same\n"
            "    Value: 1\n"
            "  - Key: same\n"
            "    Value: 2\n");
        Counts read;
        JBro::ReflectedYamlError error;
        Check(false == ReadAs(text, type, &read, error), "a key named twice must be refused");
        // **이유가 맞아야 한다.** 이 검사가 없어도 뒤이은 넣기가 거절해서 실패는 난다 -
        // 그러면 사람은 "표가 원소를 거절했다" 는 말만 보고 파일에서 무엇을 찾을지 모른다.
        Check(std::strstr(error.message.c_str(), "twice") != nullptr,
            "and say that the key came twice, not merely that something was refused");
    }

    // 항목은 `Key` 와 `Value` 둘뿐이다. 모르는 키를 조용히 넘기면 그 값이 사라진다.
    void TestATableEntryWithAnExtraKeyIsRefused()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Counts>::Get();
        const JBro::String text(
            "Value:\n"
            "  - Key: gold\n"
            "    Value: 12\n"
            "    Note: kept for the shop\n");
        Counts read;
        JBro::ReflectedYamlError error;
        Check(false == ReadAs(text, type, &read, error),
            "an entry carrying something besides Key and Value must be refused");
        Check(false == error.message.empty(), "and say why");
    }

    // 읽히지 않는 원소 하나가 배열 전체의 실패다. 기본값으로 대신하지 않는다.
    void TestAnUnreadableElementFailsTheRead()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<FloatList>::Get();
        const JBro::String text(
            "Value:\n"
            "  - 1\n"
            "  - not a number\n");
        FloatList read;
        JBro::ReflectedYamlError error;
        Check(false == ReadAs(text, type, &read, error), "an unreadable element must fail the read");
        Check(false == error.message.empty(), "and say why");
    }
}

int RunReflectedYamlTests()
{
    TestAnArrayOfNumbersGoesThereAndBack();
    TestAnEmptyArrayStaysEmpty();
    TestPackedElementsAreWrittenTheLegacyWay();
    TestArraysOfArraysKeepTheirShape();
    TestStructElementsAreWrittenAsMaps();
    TestATableIsWrittenInKeyOrder();
    TestATableOfArraysGoesThereAndBack();
    TestATableNamingAKeyTwiceIsRefused();
    TestATableEntryWithAnExtraKeyIsRefused();
    TestAnUnreadableElementFailsTheRead();
    std::cout << "Reflected YAML tests passed.\n";
    return 0;
}
