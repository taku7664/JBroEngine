#include <JBro/Reflection/Field.h>
#include <JBro/Types/NameTable.h>

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

    // 실제 컴포넌트가 생길 모양이다. 가상 함수가 있는 파생 클래스라야
    // 접근자가 vtable 너머의 필드에 닿는지 볼 수 있다.
    struct FakeComponentBase
    {
        virtual ~FakeComponentBase() = default;
        int baseField = 0;
    };

    class Probe final : public FakeComponentBase
    {
        JBRO_REFLECT_BODY(Probe)

        JBRO_FIELD(int, FieldRows, Range(4, 40) | Category("Field")) = 20;
        JBRO_FIELD(float, DropSeconds, Name("낙하 간격") | Tooltip("한 칸 떨어지는 데 걸리는 시간")) = 0.5f;
        JBRO_FIELD(float, Elapsed, NoSerialize()) = 0.0f;
        JBRO_FIELD(bool, Paused, ReadOnly()) = false;
        JBRO_FIELD(double, Plain) = 1.25;
        JBRO_FIELD(float, RangeOnly, Range(0, 1)) = 0.5f;
        JBRO_FIELD(int, Hidden, Category("Debug") | NoSerialize()) = 0;
    };

    // 필드가 하나도 없는 타입도 표를 물어볼 수 있어야 한다.
    class Empty final
    {
        JBRO_REFLECT_BODY(Empty)
    };

    // §4.1 이 기댄 것은 컴파일러 서명 형식이다. 툴체인을 올려서 그 형식이 바뀌면
    // 이름이 조용히 틀리는 대신 여기서 컴파일이 멈춰야 한다.
    static_assert(JBro::Detail::FieldName<&Probe::FieldRows>::View == "FieldRows",
        "the member name must come out of the compiler signature unchanged");
    static_assert(JBro::Detail::FieldName<&Probe::DropSeconds>::View == "DropSeconds",
        "the member name must come out of the compiler signature unchanged");

    // 개수는 END 매크로 없이 스스로 센다.
    static_assert(JBro::Detail::CountFields<Probe>() == 7, "every declared field must be counted");
    static_assert(JBro::Detail::CountFields<Empty>() == 0, "a type with no fields counts zero");
    static_assert(false == JBro::Detail::HasGapAfter<Probe, 7>(),
        "nothing may sit past the last field index");

    void TestTableHasEveryFieldInOrder()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Probe>();
        Check(table.count == 7, "the table must hold every declared field");
        Check(table.properties != nullptr, "a table with fields must point at them");

        // 선언 순서가 곧 인스펙터 순서다. 기존 엔진은 이것을 손으로 유지했다.
        const char* const expected[] = { "FieldRows", "DropSeconds", "Elapsed", "Paused", "Plain", "RangeOnly", "Hidden" };
        for (std::uint32_t i = 0; i < table.count; ++i)
        {
            const char* actual = JBro::NameTable::Get().Resolve(table.properties[i].name);
            Check(std::strcmp(actual, expected[i]) == 0,
                "fields must land in the table in the order they were declared");
        }
    }

    void TestAccessorReachesTheFieldPastTheVtable()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Probe>();
        Probe probe;

        const JBro::PropertyInfo& rows = table.properties[0];
        void* field = rows.Address(&probe);
        Check(field != &probe, "a polymorphic object keeps its fields past the vtable");
        *static_cast<int*>(field) = 99;
        Check(probe.FieldRows == 99, "writing through the accessor must reach the field");

        const void* constField = rows.ConstAddress(&probe);
        Check(constField == field, "both accessors must point at the same field");
    }

    void TestFieldTypeComesFromTheTypeDescriptor()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Probe>();

        // 크기는 프로퍼티가 아니라 타입이 안다 — 같은 사실을 두 군데 적지 않는다.
        Check(table.properties[0].type->size == sizeof(int), "int must report its own size");
        Check(table.properties[1].type->size == sizeof(float), "float must report its own size");
        Check(table.properties[4].type->size == sizeof(double), "double must report its own size");

        // 코덱이 있어야 저장이 된다. 스칼라는 전부 붙어 있어야 한다.
        for (std::uint32_t i = 0; i < table.count; ++i)
        {
            Check(table.properties[i].type->codec != nullptr,
                "every scalar field must carry a codec, or it cannot be saved");
        }

        // 같은 타입의 두 필드는 설명서 하나를 공유한다. 필드마다 새로 만들면
        // PropertyInfo::type 을 비교하는 쪽이 같은 타입을 다르다고 본다.
        Check(table.properties[1].type == table.properties[2].type,
            "two fields of the same type must share one descriptor");

        // 통째로 쓴 값이 실제로 그 필드를 통과하는지 본다.
        Probe probe;
        const JBro::ValueCodec& codec = *table.properties[1].type->codec;
        Check(codec.FromText(table.properties[1].Address(&probe), "2.5", 3),
            "the codec must read a written value");
        Check(probe.DropSeconds == 2.5f, "the codec must land on the field the property points at");
    }

    void TestAttributesArriveWhereTheInspectorLooks()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Probe>();

        // Range | Category
        const JBro::PropertyInfo& rows = table.properties[0];
        Check(rows.edit != nullptr, "a field with attributes must carry editor metadata");
        Check(rows.edit->hasRange && rows.edit->rangeMin == 4.0f && rows.edit->rangeMax == 40.0f,
            "the range must survive the merge with the category");
        Check(rows.edit->category != nullptr && std::strcmp(rows.edit->category, "Field") == 0,
            "the category must survive the merge with the range");
        Check(rows.serialize, "a field saves unless it says otherwise");

        // Name | Tooltip
        const JBro::PropertyInfo& drop = table.properties[1];
        Check(drop.edit != nullptr && drop.edit->displayName != nullptr,
            "a display name must reach the inspector");
        Check(std::strcmp(drop.edit->displayName, "낙하 간격") == 0,
            "the display name must arrive byte for byte");
        Check(drop.edit->hasRange == false, "a field without a range must not claim one");

        // NoSerialize 는 저장에만 영향을 준다. 인스펙터 메타데이터가 아니다.
        const JBro::PropertyInfo& elapsed = table.properties[2];
        Check(elapsed.serialize == false, "NoSerialize must keep the field out of the save file");
        Check(elapsed.edit == nullptr,
            "a field whose only attribute is NoSerialize carries no editor metadata");

        // ReadOnly 는 인스펙터에만 영향을 준다. 저장은 계속한다.
        const JBro::PropertyInfo& paused = table.properties[3];
        Check(paused.edit != nullptr && paused.edit->editable == false,
            "ReadOnly must reach the inspector");
        Check(paused.serialize, "ReadOnly must not stop the field from being saved");

        // 어트리뷰트가 없으면 편집 메타데이터도 없다 — 게임 빌드가 통째로 건너뛴다.
        Check(table.properties[4].edit == nullptr,
            "a field with no attributes must not allocate editor metadata");

        // 범위 하나만 붙은 필드. 다른 어트리뷰트가 없어도 편집 메타데이터가 생겨야 한다.
        const JBro::PropertyInfo& rangeOnly = table.properties[5];
        Check(rangeOnly.edit != nullptr, "a range alone must still reach the inspector");
        Check(rangeOnly.edit->hasRange && rangeOnly.edit->rangeMin == 0.0f && rangeOnly.edit->rangeMax == 1.0f,
            "the range must arrive with both ends");
        Check(rangeOnly.edit->displayName == nullptr && rangeOnly.edit->category == nullptr,
            "a field must not gain metadata it never asked for");

        // 끄는 어트리뷰트가 합쳐질 때도 꺼진 채로 남아야 한다.
        const JBro::PropertyInfo& hidden = table.properties[6];
        Check(hidden.serialize == false,
            "NoSerialize must survive being merged with another attribute");
        Check(hidden.edit != nullptr && hidden.edit->category != nullptr
            && std::strcmp(hidden.edit->category, "Debug") == 0,
            "the category must survive being merged with NoSerialize");
    }

    void TestTheTableIsBuiltOnce()
    {
        // PropertyInfo::type 과 PropertyTable::properties 의 주소를 들고 다니는 곳이 있다.
        // 물어볼 때마다 새로 만들면 그 주소들이 서로 다른 표를 가리킨다.
        const JBro::PropertyTable& first  = JBro::GetPropertyTable<Probe>();
        const JBro::PropertyTable& second = JBro::GetPropertyTable<Probe>();
        Check(&first == &second, "the table must be built once and handed out by address");
        Check(first.properties == second.properties, "the property array must not move");
    }

    void TestATypeWithNoFieldsSaysSo()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Empty>();
        Check(table.count == 0, "a type with no fields must report zero");
        Check(table.properties == nullptr,
            "an empty table must not hand out a pointer the caller could read");
    }
}

int RunReflectionFieldTests()
{
    TestTableHasEveryFieldInOrder();
    TestAccessorReachesTheFieldPastTheVtable();
    TestFieldTypeComesFromTheTypeDescriptor();
    TestAttributesArriveWhereTheInspectorLooks();
    TestTheTableIsBuiltOnce();
    TestATypeWithNoFieldsSaysSo();
    std::cout << "Reflection field tests passed.\n";
    return 0;
}
