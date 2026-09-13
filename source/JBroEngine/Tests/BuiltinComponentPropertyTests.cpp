#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Reflection/PropertyRegistry.h>
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

    const JBro::PropertyTable& Table(const char* typeName)
    {
        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(typeName);
        Check(table != nullptr, "a builtin component must be findable by its own type name");
        return *table;
    }

    const JBro::PropertyInfo& Field(const JBro::PropertyTable& table, const char* name)
    {
        for (std::uint32_t i = 0; i < table.count; ++i)
        {
            if (std::strcmp(JBro::NameTable::Get().Resolve(table.properties[i].name), name) == 0)
            {
                return table.properties[i];
            }
        }
        Check(false, "a field the component declares must be in its table");
        return table.properties[0];
    }

    void TestEveryBuiltinComponentIsThere()
    {
        Check(JBro::Component::RegisterBuiltinComponentProperties2D(),
            "every builtin 2D component must register");

        // 개수를 적어 두면 필드를 더하거나 지울 때 테스트가 먼저 운다.
        // 그 자체가 목적이다 — 인스펙터에 뭐가 보일지가 조용히 바뀌지 않게 한다.
        Check(Table("Component::Transform2D").count == 8, "Transform2D declares eight fields");
        Check(Table("Component::Camera2D").count == 6, "Camera2D declares six fields");
        Check(Table("Component::SpriteRenderer2D").count == 10, "SpriteRenderer2D declares ten fields");
        Check(Table("Component::Rigidbody2D").count == 7, "Rigidbody2D declares seven fields");
        Check(Table("Component::Collider2D").count == 5, "Collider2D declares five fields");

        // 두 번 불러도 된다. 부르는 쪽이 순서를 신경 쓰지 않아도 되게 한다.
        Check(JBro::Component::RegisterBuiltinComponentProperties2D(),
            "registering twice must not turn into a failure");
    }

    void TestAPropertyReachesTheRealMember()
    {
        const JBro::PropertyTable& table = Table("Component::Transform2D");
        JBro::Component::Transform2D transform;

        // position 은 구조체다. 그 안의 y 까지 두 단계로 닿는다.
        const JBro::PropertyInfo& position = Field(table, "position");
        Check(position.type->fields != nullptr, "Vec2 must decompose into its members");
        Check(position.type->fields->count == 2, "Vec2 has two members");

        void* positionAddress = position.Address(&transform);
        const JBro::PropertyInfo& y = position.type->fields->properties[1];
        Check(std::strcmp(JBro::NameTable::Get().Resolve(y.name), "y") == 0,
            "the members must come out in declaration order");
        Check(y.type->codec->FromText(y.Address(positionAddress), "7.5", 3),
            "the leaf must be writable");
        Check(transform.position.y == 7.5f, "the write must land on the component's real member");
        Check(transform.position.x == 0.0f, "it must land on that member only");

        // 컴포넌트는 가상 함수를 가진 파생 클래스다. 접근자는 그걸 신경 쓰지 않는다.
        const JBro::PropertyInfo& rotation = Field(table, "rotation");
        Check(rotation.Address(&transform) != &transform,
            "a component keeps its fields past the vtable");
        Check(rotation.type->codec->FromText(rotation.Address(&transform), "1.25", 4),
            "a scalar field must be writable");
        Check(transform.rotation == 1.25f, "the scalar write must reach the member");
    }

    void TestTheCachesAreNotSavedAndNotEditable()
    {
        const JBro::PropertyTable& table = Table("Component::Transform2D");

        // 월드 캐시는 저작 값에서 다시 계산된다. 저장하면 두 벌이 되고,
        // 인스펙터에서 고쳐 봐야 다음 갱신에 덮인다.
        const char* const caches[] =
        {
            "world", "worldPosition", "worldRotation", "worldScale", "worldValid"
        };
        for (const char* name : caches)
        {
            const JBro::PropertyInfo& cache = Field(table, name);
            Check(cache.serialize == false, "a world cache must not be written to the save file");
            Check(cache.edit != nullptr && cache.edit->editable == false,
                "a world cache must not be editable");
        }

        // 저작 값은 그 반대다.
        Check(Field(table, "position").serialize, "an authored value must be saved");
        Check(Field(table, "scale").serialize, "an authored value must be saved");
    }

    void TestAColorComesOutAsFourChannels()
    {
        const JBro::PropertyInfo& clearColor = Field(Table("Component::Camera2D"), "clearColor");
        Check(clearColor.type->fields != nullptr, "a color must decompose");
        Check(clearColor.type->fields->count == 4, "a color has four channels");

        const char* const channels[] = { "R", "G", "B", "A" };
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            const JBro::PropertyInfo& channel = clearColor.type->fields->properties[i];
            Check(std::strcmp(JBro::NameTable::Get().Resolve(channel.name), channels[i]) == 0,
                "the channels must come out in memory order, which is the layout contract");
            Check(channel.edit != nullptr && channel.edit->hasRange
                && channel.edit->rangeMin == 0.0f && channel.edit->rangeMax == 1.0f,
                "a channel is a linear 0..1 value and must say so");
        }

        JBro::Component::Camera2D camera;
        const JBro::PropertyInfo& green = clearColor.type->fields->properties[1];
        void* colorAddress = clearColor.Address(&camera);
        Check(green.type->codec->FromText(green.Address(colorAddress), "0.5", 3),
            "a channel must be writable");
        Check(camera.clearColor.G == 0.5f, "the channel write must reach the real member");
    }

    void TestAnEnumFieldSavesItsName()
    {
        const JBro::PropertyInfo& projection = Field(Table("Component::Camera2D"), "projection");
        Check(projection.type->enumNames != nullptr, "an enum field must carry its names");
        Check(projection.type->enumNames->count == 2, "both projections must be named");

        JBro::Component::Camera2D camera;
        char buffer[32] = {};
        std::size_t required = 0;
        Check(projection.type->codec->ToText(
                projection.ConstAddress(&camera), buffer, sizeof(buffer), required),
            "the default projection must be writable");
        Check(required == 12 && std::memcmp(buffer, "Orthographic", 12) == 0,
            "an enum field must be saved as its name");

        Check(projection.type->codec->FromText(projection.Address(&camera), "PixelPerfect", 12),
            "an enum field must read a name back");
        Check(camera.projection == JBro::Component::CameraProjection2D::PixelPerfect,
            "the enum read must reach the real member");
    }

    void TestRuntimeOnlyFieldsStayOutOfTheSaveFile()
    {
        // 에셋 참조는 둘로 나뉜다. AssetId 가 저장되는 쪽이고, AssetHandle 은
        // 이번 실행에서의 자리라 그대로 적으면 다음 실행에서 뜻 없는 숫자가 된다.
        const JBro::PropertyTable& sprite = Table("Component::SpriteRenderer2D");
        Check(Field(sprite, "spriteId").serialize,
            "the persistent asset id is what a scene remembers");
        Check(Field(sprite, "materialId").serialize,
            "the persistent asset id is what a scene remembers");
        Check(Field(sprite, "sprite").serialize == false,
            "a runtime asset handle must not be written to the save file");
        Check(Field(sprite, "material").serialize == false,
            "a runtime asset handle must not be written to the save file");

        // 핸들은 spriteId 에서 해석되는 값이다. 인스펙터에서 직접 고치면
        // 다음 해석에 덮이고, 그 사이에만 어긋난다 — 월드 캐시와 같은 이유다.
        for (const char* name : { "sprite", "material" })
        {
            const JBro::PropertyInfo& handle = Field(sprite, name);
            Check(handle.edit != nullptr && handle.edit->editable == false,
                "a resolved handle must not be editable");
            Check(handle.edit->tooltip != nullptr,
                "a resolved handle must say which field it came from");
        }

        // 저장되는 쪽은 실제로 값이 실려야 한다. 8바이트 정수 하나다.
        const JBro::PropertyInfo& id = Field(sprite, "spriteId");
        Check(id.type->fields != nullptr && id.type->fields->count == 1,
            "an asset id carries one value");
        JBro::Component::SpriteRenderer2D renderer;
        const JBro::PropertyInfo& idValue = id.type->fields->properties[0];
        Check(idValue.type->codec->FromText(idValue.Address(id.Address(&renderer)), "42", 2),
            "an asset id must be writable through its property");
        Check(renderer.spriteId.value == 42, "the write must reach the real member");

        Check(Field(sprite, "tint").serialize, "an authored value must still be saved");

        // 속도는 시뮬레이션이 매 프레임 다시 쓴다.
        const JBro::PropertyTable& body = Table("Component::Rigidbody2D");
        Check(Field(body, "linearVelocity").serialize == false,
            "a simulated value must not be restored from a file");
        Check(Field(body, "angularVelocity").serialize == false,
            "a simulated value must not be restored from a file");
        Check(Field(body, "mass").serialize, "an authored value must still be saved");
    }

    // 필드를 타고 끝까지 내려가면 반드시 코덱을 만나야 한다. 그러지 않는 잎사귀가
    // 하나라도 있으면 그 값은 저장할 방법이 없다 — 직렬화기를 쓰기 전에 여기서 안다.
    void WalkEveryLeaf(const JBro::PropertyTable& table, const char* owner, int depth)
    {
        Check(depth < 8, "a type must not contain itself");
        for (std::uint32_t i = 0; i < table.count; ++i)
        {
            const JBro::TypeDescriptor* type = table.properties[i].type;
            Check(type != nullptr, "every property must name a type");
            Check(table.properties[i].Address != nullptr && table.properties[i].ConstAddress != nullptr,
                "every property must be reachable through both accessors");
            Check(type->size > 0, "every type must know its own size");

            // 구조체는 필드로 말하고 잎사귀는 코덱으로 말한다. 둘 다이거나 둘 다 아니면
            // 저장할 때 어느 쪽을 믿을지가 갈린다.
            Check((type->fields != nullptr) != (type->codec != nullptr),
                "a type must speak through its fields or through a codec, and not both");

            if (type->fields != nullptr)
            {
                WalkEveryLeaf(*type->fields, owner, depth + 1);
                continue;
            }
            Check(type->codec->ToText != nullptr && type->codec->FromText != nullptr,
                "a leaf must be able to go to text and back, or it cannot be saved");
        }
    }

    void TestEveryLeafOfEveryBuiltinCanBeSaved()
    {
        const char* const components[] =
        {
            "Component::Transform2D",
            "Component::Camera2D",
            "Component::SpriteRenderer2D",
            "Component::Rigidbody2D",
            "Component::Collider2D",
        };
        for (const char* name : components)
        {
            WalkEveryLeaf(Table(name), name, 0);
        }
    }
}

int RunBuiltinComponentPropertyTests()
{
    TestEveryBuiltinComponentIsThere();
    TestAPropertyReachesTheRealMember();
    TestTheCachesAreNotSavedAndNotEditable();
    TestAColorComesOutAsFourChannels();
    TestAnEnumFieldSavesItsName();
    TestRuntimeOnlyFieldsStayOutOfTheSaveFile();
    TestEveryLeafOfEveryBuiltinCanBeSaved();
    std::cout << "Builtin component property tests passed.\n";
    return 0;
}
