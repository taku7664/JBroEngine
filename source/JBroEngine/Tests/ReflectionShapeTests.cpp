#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/ScalarCodec.h>
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
    // 접근자가 오프셋 없이 동작하는지 볼 수 있다.
    struct FakeComponentBase
    {
        virtual ~FakeComponentBase() = default;
        int baseField = 0;
    };

    struct Probe final : FakeComponentBase
    {
        float speed = 5.0f;
        int   health = 100;
        bool  visible = true;

    private:
        // private 이어도 클래스 안에서 접근자를 만들면 잡힌다.
        float secret = 1.5f;

    public:
        static void* SecretAddress(void* owner) noexcept
        {
            return &(static_cast<Probe*>(owner)->secret);
        }
        float GetSecret() const
        {
            return secret;
        }
    };

    void TestScalarCodecRoundTrip()
    {
        const JBro::ValueCodec& codec = JBro::GetScalarCodec<float>();
        Check(codec.ToText != nullptr && codec.FromText != nullptr
            && codec.Equals != nullptr && codec.Assign != nullptr,
            "a scalar codec must fill every entry");

        float value = 12.5f;
        char buffer[32] = {};
        std::size_t required = 0;
        Check(codec.ToText(&value, buffer, sizeof(buffer), required),
            "a value must fit a generous buffer");
        Check(required > 0 && required < sizeof(buffer), "the written length must be reported");

        float restored = 0.0f;
        Check(codec.FromText(&restored, buffer, required), "the written text must read back");
        Check(codec.Equals(&value, &restored), "a round trip must land on the same value");
    }

    void TestCodecRefusesRatherThanGuessing()
    {
        const JBro::ValueCodec& codec = JBro::GetScalarCodec<int>();

        // 버퍼가 모자라면 쓰지 않고, 필요한 크기를 알려 준다.
        int value = 123456;
        char tiny[2] = {};
        std::size_t required = 0;
        Check(false == codec.ToText(&value, tiny, sizeof(tiny), required),
            "a buffer that cannot hold the value must be refused");
        Check(required == 6, "the refusal must still report the size the caller needs");

        // 남는 글자가 있으면 실패다. "12abc" 를 12 로 받아들이지 않는다.
        int parsed = -1;
        Check(false == codec.FromText(&parsed, "12abc", 5),
            "trailing text must fail rather than parse a prefix");
        Check(parsed == -1, "a failed parse must not touch the value");

        Check(false == codec.FromText(&parsed, "", 0), "empty text must fail");
    }

    void TestBoolCodecUsesWords()
    {
        const JBro::ValueCodec& codec = JBro::GetBoolCodec();
        bool value = true;
        char buffer[8] = {};
        std::size_t required = 0;
        Check(codec.ToText(&value, buffer, sizeof(buffer), required), "true must write");
        Check(required == 4 && std::memcmp(buffer, "true", 4) == 0,
            "a bool must be written as a word, not as a digit");

        bool restored = false;
        Check(codec.FromText(&restored, "false", 5), "false must read");
        Check(restored == false, "false must read back as false");
        // 표기를 하나로 두는지 본다.
        Check(false == codec.FromText(&restored, "1", 1), "a digit must not be accepted as a bool");
    }

    void TestPropertyReachesAFieldWithoutAnOffset()
    {
        JBro::TypeDescriptor floatType = JBro::MakeScalarTypeDescriptor<float>("float");
        Check(floatType.size == sizeof(float) && floatType.alignment == alignof(float),
            "a scalar descriptor must carry its own size and alignment");
        Check(floatType.codec != nullptr, "a scalar descriptor must carry a codec");
        Check(floatType.arrayOps == nullptr && floatType.tableOps == nullptr,
            "a scalar is neither an array nor a table, and the ops say so");

        JBro::PropertyInfo speed;
        speed.name = JBro::NameTable::Get().Intern("speed");
        speed.type = &floatType;
        speed.Address = [](void* owner) noexcept -> void*
        {
            return &(static_cast<Probe*>(owner)->speed);
        };

        Probe probe;
        // 가상 함수 때문에 필드는 오브젝트 시작이 아니다. 접근자는 그걸 신경 쓰지 않는다.
        void* field = speed.Address(&probe);
        Check(field != &probe, "a polymorphic object keeps its fields past the vtable");
        *static_cast<float*>(field) = 42.0f;
        Check(probe.speed == 42.0f, "writing through the accessor must reach the field");

        // private 멤버도 같은 방식으로 닿는다 — 접근자가 클래스 안에서 만들어졌기 때문이다.
        JBro::PropertyInfo secret;
        secret.type = &floatType;
        secret.Address = &Probe::SecretAddress;
        *static_cast<float*>(secret.Address(&probe)) = 7.25f;
        Check(probe.GetSecret() == 7.25f, "an accessor made inside the class reaches a private member");
    }

    void TestPropertyCarriesNoTypeOfItsOwn()
    {
        // 기존 엔진은 Type 과 Size 를 프로퍼티에도, 타입 명세에도 적었고
        // 주석이 "같은 값·같은 의미" 라고 인정했다. 여기서는 한 군데뿐이어야 한다.
        JBro::TypeDescriptor intType = JBro::MakeScalarTypeDescriptor<int>("int");
        JBro::PropertyInfo health;
        health.type = &intType;

        Check(health.type->size == sizeof(int),
            "size must be readable through the type, since the property has none");
        Check(health.edit == nullptr,
            "editor metadata must be optional so a game build can drop it");
        Check(health.serialize, "a property saves by default");
    }
}

int RunReflectionShapeTests()
{
    TestScalarCodecRoundTrip();
    TestCodecRefusesRatherThanGuessing();
    TestBoolCodecUsesWords();
    TestPropertyReachesAFieldWithoutAnOffset();
    TestPropertyCarriesNoTypeOfItsOwn();
    std::cout << "Reflection shape tests passed.\n";
    return 0;
}
