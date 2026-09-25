#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
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
        Check(Table("Component::SpriteRenderer2D").count == 13, "SpriteRenderer2D declares thirteen fields");
        Check(Table("Component::Text2D").count == 14, "Text2D declares fourteen fields");
        Check(Table("Component::Rigidbody2D").count == 7, "Rigidbody2D declares seven fields");
        Check(Table("Component::Collider2D").count == 10,
            "Collider2D declares ten fields - shape, size and trigger, then points and the surface and filter of D-199");

        // 두 번 불러도 된다. 부르는 쪽이 순서를 신경 쓰지 않아도 되게 한다.
        Check(JBro::Component::RegisterBuiltinComponentProperties2D(),
            "registering twice must not turn into a failure");
    }

    void TestEvery3DBuiltinComponentIsThere()
    {
        Check(JBro::Component::RegisterBuiltinComponentProperties3D(),
            "every builtin 3D component must register");

        // 3D 도 2D 와 같은 모양이 됐다(framework3d-plan §2.1). 저작 값 셋 + 월드 캐시 넷.
        Check(Table("Component::Transform3D").count == 7, "Transform3D declares seven fields");
        Check(Table("Component::Camera3D").count == 7, "Camera3D declares seven fields");
        Check(Table("Component::MeshRenderer3D").count == 6, "MeshRenderer3D declares six fields");
        Check(Table("Component::Rigidbody3D").count == 2, "Rigidbody3D declares two fields");
        Check(Table("Component::Collider3D").count == 1, "Collider3D declares one field");

        Check(JBro::Component::RegisterBuiltinComponentProperties3D(),
            "registering twice must not turn into a failure");

        // 2D 와 3D 는 같은 보관함에 있고 이름이 갈린다. 한쪽 등록이 다른 쪽을 밀어내지 않는다.
        Check(JBro::PropertyRegistry::Lookup("Component::Transform2D") != nullptr,
            "registering the 3D components must not displace the 2D ones");
    }

    void TestA3DPositionHasThreeAxes()
    {
        const JBro::PropertyInfo& position = Field(Table("Component::Transform3D"), "position");
        Check(position.type->fields != nullptr, "Vec3 must decompose into its members");
        Check(position.type->fields->count == 3, "Vec3 has three members");

        const char* const axes[] = { "x", "y", "z" };
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            Check(std::strcmp(JBro::NameTable::Get().Resolve(
                    position.type->fields->properties[i].name), axes[i]) == 0,
                "the axes must come out in declaration order");
        }

        // 축이 서로 다른 멤버를 가리키는지 본다. 이름만 맞고 주소가 겹치면
        // 한 축에 쓴 값이 다른 축을 덮는다.
        JBro::Component::Transform3D transform;
        void* address = position.Address(&transform);
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            const JBro::PropertyInfo& axis = position.type->fields->properties[i];
            Check(axis.type->codec->FromText(axis.Address(address), "1", 1), "an axis must be writable");
        }
        Check(transform.position.x == 1.0f && transform.position.y == 1.0f
            && transform.position.z == 1.0f,
            "every axis must reach a member of its own");
    }

    void TestA3DRotationIsStoredAsFourComponents()
    {
        const JBro::PropertyInfo& rotation = Field(Table("Component::Transform3D"), "rotation");
        Check(rotation.type->fields != nullptr, "a quaternion must decompose");
        Check(rotation.type->fields->count == 4, "a quaternion has four components");

        // 오일러각으로 저장하면 짐벌락과 각도 규약이 파일 형식에 들어온다.
        const char* const components[] = { "x", "y", "z", "w" };
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            Check(std::strcmp(JBro::NameTable::Get().Resolve(
                    rotation.type->fields->properties[i].name), components[i]) == 0,
                "a rotation is saved as its components, not as angles");
        }

        JBro::Component::Transform3D transform;
        const JBro::PropertyInfo& w = rotation.type->fields->properties[3];
        Check(w.type->codec->FromText(w.Address(rotation.Address(&transform)), "0.25", 4),
            "a component of the rotation must be writable");
        Check(transform.rotation.w == 0.25f, "the write must reach the real member");
        Check(transform.rotation.x == 0.0f, "it must reach that component only");
    }

    void Test3DFollowsTheSameSaveRulesAs2D()
    {
        // 에셋 참조는 여기서도 id 와 핸들로 나뉜다.
        const JBro::PropertyTable& mesh = Table("Component::MeshRenderer3D");
        Check(Field(mesh, "meshId").serialize, "the persistent asset id is what a scene remembers");
        Check(Field(mesh, "materialId").serialize, "the persistent asset id is what a scene remembers");
        for (const char* name : { "mesh", "material" })
        {
            const JBro::PropertyInfo& handle = Field(mesh, name);
            Check(handle.serialize == false,
                "a runtime asset handle must not be written to the save file");
            Check(handle.edit != nullptr && handle.edit->editable == false,
                "a resolved handle must not be editable");
        }

        // 시뮬레이션이 다시 쓰는 값도 2D 와 같은 규칙이다.
        const JBro::PropertyTable& body = Table("Component::Rigidbody3D");
        Check(Field(body, "velocity").serialize == false,
            "a simulated value must not be restored from a file");
        Check(Field(body, "mass").serialize, "an authored value must still be saved");
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

        // 저장되는 쪽은 실제로 값이 실려야 한다.
        //
        // **쪼개지지 않는다.** 안에 정수가 둘 있지만 그것은 저장 방식이지 부분이 아니다 —
        // 필드로 두면 저장 파일에 `spriteId:` 아래 `high:`·`low:` 가 한 단 더 생긴다.
        const JBro::PropertyInfo& id = Field(sprite, "spriteId");
        Check(id.type->fields == nullptr, "an asset id has no parts to show");
        Check(id.type->codec != nullptr, "an asset id speaks for itself");

        JBro::Component::SpriteRenderer2D renderer;
        Check(id.type->codec->FromText(id.Address(&renderer), "0000000000000000000000000000002a", 32),
            "an asset id must be writable through its property");
        Check(renderer.spriteId.high == 0 && renderer.spriteId.low == 42, "the write must reach the real member");

        char buffer[33] = {};
        std::size_t required = 0;
        Check(id.type->codec->ToText(id.ConstAddress(&renderer), buffer, sizeof(buffer), required),
            "an asset id must be writable to a file");
        Check(required == 32 && std::memcmp(buffer, "0000000000000000000000000000002a", 32) == 0,
            "an asset id must go out as the 32 hex digits it is");

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

            // 배열은 원소로 말한다. 원소를 같은 규칙으로 내려간다 - `Collider2D::points` 가 내장 컴포넌트의 첫
            // 컨테이너 필드다(D-199). 원소가 끝내 코덱에 닿지 않으면 그 배열은 저장할 수 없다.
            while (type->arrayOps != nullptr)
            {
                Check(type->element != nullptr, "an array must name its element type");
                Check(type->fields == nullptr && type->codec == nullptr,
                    "an array speaks through its elements, not through fields or a codec of its own");
                type = type->element;
            }

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
            "Component::Transform3D",
            "Component::Camera3D",
            "Component::MeshRenderer3D",
            "Component::Rigidbody3D",
            "Component::Collider3D",
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
    TestEvery3DBuiltinComponentIsThere();
    TestA3DPositionHasThreeAxes();
    TestA3DRotationIsStoredAsFourComponents();
    Test3DFollowsTheSameSaveRulesAs2D();
    TestAPropertyReachesTheRealMember();
    TestTheCachesAreNotSavedAndNotEditable();
    TestAColorComesOutAsFourChannels();
    TestAnEnumFieldSavesItsName();
    TestRuntimeOnlyFieldsStayOutOfTheSaveFile();
    TestEveryLeafOfEveryBuiltinCanBeSaved();
    std::cout << "Builtin component property tests passed.\n";
    return 0;
}
