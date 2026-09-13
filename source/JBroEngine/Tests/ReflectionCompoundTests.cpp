#include <JBro/Reflection/EnumDescriptor.h>
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

    // 값이 연속이 아니다. 인덱스로 값을 지어내면 여기서 틀린다.
    enum class Shape : std::uint8_t
    {
        Box     = 1,
        Circle  = 4,
        Capsule = 9
    };

    // 핫 경로에 있는 타입 자리. 매크로를 넣지 않고 밖에서 표를 엮는다.
    struct FakeVec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };
}

// 설명서는 그것을 필드로 쓰는 타입보다 **먼저** 와야 한다. JBRO_FIELD 가 만드는 함수의
// 본문은 클래스가 닫히는 자리에서 컴파일되고, 거기서 TypeDescriptorOf 를 집기 때문이다.
namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Shape, "Test::Shape",
        { Shape::Box,     "Box" },
        { Shape::Circle,  "Circle" },
        { Shape::Capsule, "Capsule" });

    template <>
    struct TypeDescriptorOf<FakeVec2>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&FakeVec2::x>(),
                MakeFieldEntry<&FakeVec2::y>(),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<FakeVec2>("Test::FakeVec2", fields.Get());
            return descriptor;
        }
    };
}

namespace
{
    // 그 두 타입을 필드로 가지는 컴포넌트 자리.
    class Collider final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::FakeCollider";
        }

        JBRO_REFLECT_BODY(Collider)

        JBRO_FIELD(Shape, shape) = Shape::Box;
        JBRO_FIELD(FakeVec2, offset);
        JBRO_FIELD(float, radius, Range(0, 100)) = 0.5f;
    };

    void TestAnEnumSavesItsNameNotItsNumber()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Shape>::Get();
        Check(type.enumNames != nullptr, "an enum must carry its names");
        Check(type.enumNames->count == 3, "every named value must be there");
        Check(type.codec != nullptr, "an enum is a leaf value and needs a codec");
        Check(type.fields == nullptr, "an enum has no fields of its own");

        Shape value = Shape::Circle;
        char buffer[16] = {};
        std::size_t required = 0;
        Check(type.codec->ToText(&value, buffer, sizeof(buffer), required), "an enum must write");
        Check(required == 6 && std::memcmp(buffer, "Circle", 6) == 0,
            "an enum must be written as its name, so inserting a value later cannot shift old files");

        Shape restored = Shape::Box;
        Check(type.codec->FromText(&restored, "Capsule", 7), "a name must read back");
        Check(restored == Shape::Capsule, "the name must land on the value it was written from");
    }

    void TestAnEnumRefusesWhatItDoesNotKnow()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Shape>::Get();

        // 모르는 이름을 첫 값으로 떨어뜨리면 지워진 enum 값이 조용히 다른 것이 된다.
        Shape value = Shape::Capsule;
        Check(false == type.codec->FromText(&value, "Triangle", 8), "an unknown name must fail");
        Check(value == Shape::Capsule, "a failed read must not touch the value");

        // 숫자로도 안 받는다. 표기를 하나로 둔다.
        Check(false == type.codec->FromText(&value, "4", 1),
            "a number must not be accepted as an enum");

        // 이름 없는 값은 지어내서 쓰지 않는다.
        Shape stray = static_cast<Shape>(77);
        char buffer[16] = {};
        std::size_t required = 1;
        Check(false == type.codec->ToText(&stray, buffer, sizeof(buffer), required),
            "a value with no name must not be written");
        Check(required == 0, "a refusal must not claim a size the caller could trust");
    }

    void TestTheEnumIndexIsForTheDropdown()
    {
        const JBro::EnumNames& names = *JBro::TypeDescriptorOf<Shape>::Get().enumNames;

        Shape value = Shape::Circle;
        Check(names.ToIndex(&value) == 1, "the index must be the position in the name list");
        Check(std::strcmp(names.names[1], "Circle") == 0,
            "the name list must line up with the index");

        names.FromIndex(&value, 2);
        Check(value == Shape::Capsule,
            "picking an index must set the value that name stands for, not the index itself");

        // 값이 연속이 아니므로 인덱스와 값이 같지 않다. 그것을 붙잡아 둔다.
        Check(static_cast<std::uint8_t>(value) == 9, "the stored value is not its position");

        Shape stray = static_cast<Shape>(77);
        Check(names.ToIndex(&stray) == -1, "a value with no name has no index");

        Shape untouched = Shape::Box;
        names.FromIndex(&untouched, 99);
        Check(untouched == Shape::Box, "an index past the end must leave the value alone");
    }

    void TestAStructSpeaksThroughItsFields()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<FakeVec2>::Get();
        Check(type.fields != nullptr, "a struct must hand out its fields");
        Check(type.codec == nullptr,
            "a struct speaks through its fields, so nothing may also read it as one value");
        Check(type.fields->count == 2, "both members must be there");

        const char* first = JBro::NameTable::Get().Resolve(type.fields->properties[0].name);
        Check(std::strcmp(first, "x") == 0,
            "the member name must come from the member pointer even outside the class");

        // 필드를 타고 내려가 잎사귀에서 코덱을 만난다. 소비자는 FakeVec2 를 몰라도 된다.
        FakeVec2 point;
        const JBro::PropertyInfo& y = type.fields->properties[1];
        Check(y.type->codec != nullptr, "the leaf under a struct must carry a codec");
        Check(y.type->codec->FromText(y.Address(&point), "2.5", 3),
            "writing through the leaf must work");
        Check(point.y == 2.5f, "the write must land on the member the property points at");
        Check(point.x == 0.0f, "it must land on that member only");
    }

    void TestAComponentReachesAFieldInsideAStruct()
    {
        const JBro::PropertyTable& table = JBro::GetPropertyTable<Collider>();
        Check(table.count == 3, "the component must report its own fields");

        Collider collider;

        // offset 은 구조체다. 그 안의 x 까지 두 단계로 닿는다 —
        // 직렬화기가 알아야 하는 것은 "필드가 있으면 내려간다" 하나뿐이다.
        const JBro::PropertyInfo& offset = table.properties[1];
        Check(offset.type->fields != nullptr, "the struct field must decompose");

        void* offsetAddress = offset.Address(&collider);
        const JBro::PropertyInfo& innerX = offset.type->fields->properties[0];
        Check(innerX.type->codec->FromText(innerX.Address(offsetAddress), "-3", 2),
            "the leaf inside the struct must be writable through the outer property");
        Check(collider.offset.x == -3.0f, "the nested write must reach the real member");
        Check(collider.offset.y == 0.0f, "the nested write must reach that member only");

        // enum 필드도 같은 표에서 나온다.
        const JBro::PropertyInfo& shape = table.properties[0];
        Check(shape.type->enumNames != nullptr, "the enum field must carry its names");
        Check(shape.type->codec->FromText(shape.Address(&collider), "Circle", 6),
            "the enum field must read a name");
        Check(collider.shape == Shape::Circle, "the enum write must reach the real member");
    }
}

int RunReflectionCompoundTests()
{
    TestAnEnumSavesItsNameNotItsNumber();
    TestAnEnumRefusesWhatItDoesNotKnow();
    TestTheEnumIndexIsForTheDropdown();
    TestAStructSpeaksThroughItsFields();
    TestAComponentReachesAFieldInsideAStruct();
    std::cout << "Reflection compound tests passed.\n";
    return 0;
}
