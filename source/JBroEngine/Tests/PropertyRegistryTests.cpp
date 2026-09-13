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

    // 빌트인 컴포넌트 자리에 설 타입.
    class FakeTransform final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::FakeTransform";
        }

        JBRO_REFLECT_BODY(FakeTransform)

        JBRO_FIELD(float, PositionX) = 0.0f;
        JBRO_FIELD(float, PositionY) = 0.0f;
        JBRO_FIELD(float, Rotation, Range(0, 360)) = 0.0f;
    };

    // 스크립트 자리에 설 타입.
    class FakePlayerScript final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Game::FakePlayerScript";
        }

        JBRO_REFLECT_BODY(FakePlayerScript)

        JBRO_FIELD(int, Lives, Category("Rules")) = 3;
        JBRO_FIELD(float, Speed) = 1.0f;
    };

    // 스크립트가 엔진 타입명을 덮으려 드는 경우. 이름이 FakeTransform 과 같다.
    class ImpostorTransform final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::FakeTransform";
        }

        JBRO_REFLECT_BODY(ImpostorTransform)

        JBRO_FIELD(int, NotEvenTheSameShape) = 0;
    };

    // 필드가 없는 타입. "물어봤더니 없더라" 와 "아직 등록 안 됐다" 는 다른 답이다.
    class FakeMarker final
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::FakeMarker";
        }

        JBRO_REFLECT_BODY(FakeMarker)
    };

    void TestABuiltinTypeIsFoundByName()
    {
        Check(JBro::RegisterBuiltinProperties<FakeTransform>(),
            "a builtin component must register");

        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup("Component::FakeTransform");
        Check(table != nullptr, "a registered type must be findable by its name alone");
        Check(table->count == 3, "the registered table must be the one the macro built");

        const char* first = JBro::NameTable::Get().Resolve(table->properties[0].name);
        Check(std::strcmp(first, "PositionX") == 0,
            "the table reached through the registry must carry the same fields");

        // 등록하지 않은 이름은 없다고 답해야 한다. 빈 표를 지어내면 부르는 쪽이
        // 필드가 없는 타입과 모르는 타입을 구별하지 못한다.
        Check(JBro::PropertyRegistry::Lookup("Component::NeverRegistered") == nullptr,
            "an unregistered name must come back as nothing");
    }

    void TestTheSameNameIsRefusedTwice()
    {
        // 이미 위에서 한 번 등록했다. 조용히 덮으면 어느 쪽 표를 보는지 알 수 없다.
        Check(false == JBro::RegisterBuiltinProperties<FakeTransform>(),
            "registering the same name twice must be refused");
    }

    void TestATypeWithNoFieldsStillRegisters()
    {
        Check(JBro::RegisterBuiltinProperties<FakeMarker>(),
            "a type with no fields must still be registerable");

        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup("Component::FakeMarker");
        Check(table != nullptr, "a type with no fields must answer 'none', not 'unknown'");
        Check(table->count == 0, "a type with no fields reports zero");
    }

    void TestAMalformedRegistrationIsRefused()
    {
        JBro::PropertyRegistry local;

        // 이름 없는 항목. InvalidNameId 는 0 이고 MakeNameId(nullptr) 도 0 이라,
        // 받아 주면 Lookup(nullptr) 이 그것을 찾아낸다.
        const JBro::PropertyTable* real = JBro::PropertyRegistry::Lookup("Component::FakeTransform");
        Check(false == local.Register(JBro::InvalidNameId, *real),
            "a table with no name must be refused");
        Check(local.GetCount() == 0, "a refused registration must leave nothing behind");

        // 개수와 배열이 어긋난 항목. 읽는 쪽은 count 만 믿고 도므로 그대로 터진다.
        JBro::PropertyTable claimsThree;
        claimsThree.properties = nullptr;
        claimsThree.count = 3;
        Check(false == local.Register(JBro::MakeNameId("Broken::ClaimsThree"), claimsThree),
            "a table that claims fields it cannot hand out must be refused");

        JBro::PropertyTable claimsNone;
        claimsNone.properties = real->properties;
        claimsNone.count = 0;
        Check(false == local.Register(JBro::MakeNameId("Broken::ClaimsNone"), claimsNone),
            "a table that reports zero while pointing at fields must be refused");

        Check(local.GetCount() == 0, "none of the malformed tables may have landed");
    }

    void TestScriptsLiveInTheOtherTable()
    {
        Check(JBro::RegisterScriptProperties<FakePlayerScript>(),
            "a script type must register");

        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup("Game::FakePlayerScript");
        Check(table != nullptr, "a script type must be findable through the same lookup");
        Check(table->count == 2, "the script table must carry the script's fields");

        // 섞이지 않았는지 본다. 같은 그릇에 있으면 DLL 이 내려갈 때 골라내야 한다.
        const JBro::NameId scriptName = JBro::MakeNameId("Game::FakePlayerScript");
        Check(JBro::PropertyRegistry::Builtin().Find(scriptName) == nullptr,
            "a script must not land in the builtin table");
        Check(JBro::PropertyRegistry::Script().Find(scriptName) != nullptr,
            "a script must land in the script table");
    }

    void TestAScriptCannotShadowAnEngineType()
    {
        // 통과시키면 Lookup 이 조용히 빌트인 쪽을 준다. 스크립트 작성자는 자기 필드가
        // 왜 안 보이는지 알 길이 없다. 그래서 등록 자체를 거절한다.
        Check(false == JBro::RegisterScriptProperties<ImpostorTransform>(),
            "a script must not be allowed to take a name the engine already owns");

        const JBro::NameId taken = JBro::MakeNameId("Component::FakeTransform");
        Check(JBro::PropertyRegistry::Script().Find(taken) == nullptr,
            "the refused registration must leave nothing behind");

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(taken);
        Check(table != nullptr && table->count == 3,
            "the engine type must still be the one the lookup returns");
    }

    void TestClearingTheScriptTableLeavesTheEngineAlone()
    {
        // DLL 이 내려갈 때 일어나는 일이다. 스크립트 표의 함수 포인터는 사라질 코드를
        // 가리키므로 전부 지워야 하고, 빌트인은 엔진 수명이라 남아야 한다.
        const std::size_t builtinBefore = JBro::PropertyRegistry::Builtin().GetCount();
        Check(JBro::PropertyRegistry::Script().GetCount() > 0,
            "there must be something to clear for this test to mean anything");

        JBro::PropertyRegistry::Script().Clear();

        Check(JBro::PropertyRegistry::Script().GetCount() == 0,
            "unloading must leave no script table behind");
        Check(JBro::PropertyRegistry::Builtin().GetCount() == builtinBefore,
            "unloading a script module must not touch the engine's own tables");
        Check(JBro::PropertyRegistry::Lookup("Game::FakePlayerScript") == nullptr,
            "a script type must stop being findable once its module is gone");
        Check(JBro::PropertyRegistry::Lookup("Component::FakeTransform") != nullptr,
            "a builtin component must survive a script module going away");
    }

    void TestBindingSendsRegistrationsToTheHostTable()
    {
        // DLL 안에서 일어나는 일이다. 바인딩하지 않으면 DLL 은 자기 사본에 등록하고
        // 호스트는 아무것도 못 본다(D-44 와 같은 함정).
        JBro::PropertyRegistry hostTable;
        JBro::PropertyRegistry::BindScript(&hostTable);
        Check(&JBro::PropertyRegistry::Script() == &hostTable,
            "after binding, registrations must go to the bound table");

        Check(JBro::RegisterScriptProperties<FakePlayerScript>(),
            "a script must register into the bound table");
        Check(hostTable.GetCount() == 1, "the bound table must be the one that received it");
        Check(JBro::PropertyRegistry::ScriptLocal().GetCount() == 0,
            "nothing may land in this module's own copy while a table is bound");

        JBro::PropertyRegistry::BindScript(nullptr);
        Check(&JBro::PropertyRegistry::Script() == &JBro::PropertyRegistry::ScriptLocal(),
            "unbinding must fall back to this module's own table");
        Check(JBro::PropertyRegistry::Lookup("Game::FakePlayerScript") == nullptr,
            "what the bound table holds must not be visible after unbinding");
    }
}

int RunPropertyRegistryTests()
{
    TestABuiltinTypeIsFoundByName();
    TestTheSameNameIsRefusedTwice();
    TestATypeWithNoFieldsStillRegisters();
    TestAMalformedRegistrationIsRefused();
    TestScriptsLiveInTheOtherTable();
    TestAScriptCannotShadowAnEngineType();
    TestClearingTheScriptTableLeavesTheEngineAlone();
    TestBindingSendsRegistrationsToTheHostTable();
    std::cout << "Property registry tests passed.\n";
    return 0;
}
