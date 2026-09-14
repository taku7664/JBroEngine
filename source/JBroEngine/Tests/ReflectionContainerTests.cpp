#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Types/NameTable.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

// 컨테이너를 **타입을 모른 채** 만지는 길이다. 인스펙터와 직렬화가 이 길만 써야
// `Array<Vec2>` 라는 것을 아무도 몰라도 된다(D-56).

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

    // **쉼표가 든 타입은 별칭을 거쳐야 한다.** `JBRO_FIELD(Table<K, V>, name)` 은
    // 전처리기가 쉼표에서 인자를 갈라 세 개로 본다 - 매크로의 한계이고, 별칭
    // 하나면 끝나므로 매크로를 괄호로 복잡하게 만들지 않는다.
    using CountTable = JBro::Table<JBro::String, std::int32_t>;

    // 배열을 필드로 든 타입. 컴파일이 되는 것 자체가 절반이다 -
    // `TypeDescriptorOf` 특수화가 없으면 필드 선언에서 멈춘다.
    class ArrayHolder final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::ArrayHolder";
        }

        JBRO_REFLECT_BODY(ArrayHolder)

        JBRO_FIELD(JBro::Array<float>, points);
        JBRO_FIELD(CountTable, counts);
    };

    const JBro::PropertyInfo& FieldOf(const JBro::PropertyTable& table, const char* name)
    {
        for (std::uint32_t index = 0; index < table.count; ++index)
        {
            const char* found =
                JBro::NameTable::Get().Resolve(table.properties[index].name);
            if (found != nullptr && std::strcmp(found, name) == 0)
            {
                return table.properties[index];
            }
        }
        Check(false, "the field this test names must be in the table");
        return table.properties[0];
    }

    void TestAnArrayCanBeWorkedWithoutKnowingItsType()
    {
        JBro::Array<float> points;
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<JBro::Array<float>>::Get();
        Check(type.arrayOps != nullptr, "an array type must carry array ops");
        Check(type.element != nullptr, "and must say what it holds");
        Check(std::strcmp(JBro::NameTable::Get().Resolve(type.element->typeName),
                "float") == 0,
            "which here is a float");
        Check(std::strcmp(JBro::NameTable::Get().Resolve(type.typeName),
                "Array<float>") == 0,
            "and its own name says so too");
        // **컨테이너에는 코덱도 필드도 없다.** 둘 다 있으면 저장할 때 어느 쪽을
        // 믿을지가 갈린다(D-56 의 불변식).
        Check(type.codec == nullptr && type.fields == nullptr,
            "a container speaks through its ops, not a codec or fields");

        const JBro::ArrayOps& ops = *type.arrayOps;
        void* erased = &points;
        Check(ops.GetSize(erased) == 0, "it starts empty");
        Check(ops.GetElement(erased, 0) == nullptr, "and has nothing at index zero");

        Check(ops.AddDefault(erased), "adding must work");
        Check(ops.AddDefault(erased), "twice");
        Check(ops.AddDefault(erased), "three times");
        Check(ops.GetSize(erased) == 3, "and leave three");

        // 원소 주소로 값을 쓴다. 이것이 인스펙터가 하는 일이다.
        *static_cast<float*>(ops.GetElement(erased, 0)) = 1.0f;
        *static_cast<float*>(ops.GetElement(erased, 1)) = 2.0f;
        *static_cast<float*>(ops.GetElement(erased, 2)) = 3.0f;
        Check(points[1] > 1.9f && points[1] < 2.1f, "and the real array sees it");
        Check(*static_cast<const float*>(ops.GetConstElement(erased, 2)) > 2.9f,
            "reading back const works too");

        // **가운데를 지우면 순서가 지켜져야 한다.** 배열에서 순서는 뜻이 있다.
        Check(ops.RemoveAt(erased, 1), "removing the middle must work");
        Check(ops.GetSize(erased) == 2, "leaving two");
        Check(points[0] > 0.9f && points[0] < 1.1f, "the first stays first");
        Check(points[1] > 2.9f && points[1] < 3.1f,
            "and the last moves up, rather than being swapped in from the end");

        Check(false == ops.RemoveAt(erased, 99), "removing past the end must be refused");
        Check(ops.GetSize(erased) == 2, "and change nothing");

        ops.Clear(erased);
        Check(ops.GetSize(erased) == 0, "clearing empties it");
    }

    void TestATableCanBeWalkedWithoutKnowingItsType()
    {
        JBro::Table<JBro::String, std::int32_t> counts;
        const JBro::TypeDescriptor& type =
            JBro::TypeDescriptorOf<JBro::Table<JBro::String, std::int32_t>>::Get();
        Check(type.tableOps != nullptr, "a table type must carry table ops");
        Check(type.key != nullptr && type.value != nullptr,
            "and must say what it maps to what");

        const JBro::TableOps& ops = *type.tableOps;
        void* erased = &counts;
        Check(ops.GetSize(erased) == 0, "it starts empty");
        Check(ops.BeginSlot(erased) == JBro::TableOps::InvalidSlot,
            "so walking it ends immediately");

        // **키는 타입이 지워진 쪽에서 만들 수 없다.** 그래서 만들어 주는 길이 있다.
        void* key = ops.CreateValue();
        Check(key != nullptr, "the table must be able to make a key");
        *static_cast<JBro::String*>(key) = "alpha";
        Check(ops.InsertDefault(erased, key), "inserting under that key must work");
        Check(false == ops.InsertDefault(erased, key),
            "and inserting the same key again must not");
        Check(ops.ContainsKey(erased, key), "the key must be found");
        Check(ops.GetSize(erased) == 1, "and counted");

        *static_cast<std::int32_t*>(ops.FindValue(erased, key)) = 7;
        Check(*counts.Find(JBro::String("alpha")) == 7, "the real table sees the value");

        *static_cast<JBro::String*>(key) = "beta";
        Check(ops.InsertDefault(erased, key), "a second key must go in");
        *static_cast<std::int32_t*>(ops.FindValue(erased, key)) = 9;

        // **슬롯은 조밀하지 않다.** 훑는 쪽은 커서만 따라간다.
        int seen = 0;
        int total = 0;
        for (std::size_t slot = ops.BeginSlot(erased);
            slot != JBro::TableOps::InvalidSlot;
            slot = ops.NextSlot(erased, slot))
        {
            const void* walkedKey = ops.GetKeyAt(erased, slot);
            void* walkedValue = ops.GetValueAt(erased, slot);
            Check(walkedKey != nullptr && walkedValue != nullptr,
                "an occupied slot must hand back both halves");
            total += *static_cast<std::int32_t*>(walkedValue);
            ++seen;
        }
        Check(seen == 2, "the walk must visit every entry exactly once");
        Check(total == 16, "and reach the values that are really in there");

        Check(ops.RemoveKey(erased, key), "removing by key must work");
        Check(ops.GetSize(erased) == 1, "leaving one");
        Check(false == ops.ContainsKey(erased, key), "and that key must be gone");
        Check(false == ops.RemoveKey(erased, key), "removing it twice must be refused");

        ops.DestroyValue(key);
        ops.Clear(erased);
        Check(ops.GetSize(erased) == 0, "clearing empties it");
        Check(ops.ContainsKey(erased, nullptr) == false, "nothing is not a key");
        Check(ops.FindValue(erased, nullptr) == nullptr, "and finds nothing");
    }

    // 컨테이너를 **필드로** 들 수 있어야 한다. 이것이 안 되면 위의 전부가
    // 쓸 곳 없는 기계다.
    void TestAComponentCanDeclareContainerFields()
    {
        Check(JBro::RegisterBuiltinProperties<ArrayHolder>(),
            "a type with container fields must register");
        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup(ArrayHolder::StaticTypeName());
        Check(table != nullptr, "and be findable");
        Check(table->count == 2, "with both of its fields");

        ArrayHolder holder;
        const JBro::PropertyInfo& points = FieldOf(*table, "points");
        Check(points.type != nullptr && points.type->arrayOps != nullptr,
            "the array field must reach its ops through the property");
        void* address = points.Address(&holder);
        Check(address == &holder.points, "and point at the real member");
        Check(points.type->arrayOps->AddDefault(address), "which can then be grown");
        Check(holder.points.Size() == 1, "and the member grows with it");

        const JBro::PropertyInfo& counts = FieldOf(*table, "counts");
        Check(counts.type != nullptr && counts.type->tableOps != nullptr,
            "the table field must reach its ops too");
    }
}

int RunReflectionContainerTests()
{
    TestAnArrayCanBeWorkedWithoutKnowingItsType();
    TestATableCanBeWalkedWithoutKnowingItsType();
    TestAComponentCanDeclareContainerFields();
    std::cout << "Reflection container tests passed.\n";
    return 0;
}
