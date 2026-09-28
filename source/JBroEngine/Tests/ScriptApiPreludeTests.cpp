#include <JBro/ScriptAPI.h>

#if defined(JBRO_TEST_SCRIPT_API_SYSTEM_CONTEXT_LEAK)
#include <JBro/Runtime/SystemContext.h>
#endif

#include <JBro/Types/FixedString.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

#if defined(JBRO_TEST_REF_GAMEOBJECT_LEAK)
JBro::Ref<JBro::GameObject> forbiddenGameObjectReference;
#endif

namespace
{
    template<typename T>
    concept CompleteType = requires
    {
        sizeof(T);
    };

#if defined(_MSC_VER)
    __if_exists(JBro::SystemContext)
    {
        static_assert(false == CompleteType<JBro::SystemContext>,
            "ScriptAPI.h must not expose the SystemContext definition");
    }
#endif

    JBRO_SCRIPT(PreludeScriptProbe)
    {
    };

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestScriptPreludeSurface()
    {
        static_assert(std::is_same_v<decltype(Ref<ComponentBase>::Category), const RefCategory>);
        static_assert(std::is_class_v<PreludeScriptProbe>);
        // 이 테스트 프로젝트는 두 차원의 include 경로를 모두 갖는다. 어느 프렐류드를 받았는지
        // 드러나도록 2D 전용 타입을 직접 짚는다. 실제 배타성은 스크립트 프로브가 증명한다.
        static_assert(std::is_base_of_v<ComponentBase, Component::Transform2D>);

        // **프렐류드는 `JBro` 만 연다**(D-259). `JBro::Fixed::String<N>` 은 `JBro::String` 과 이름이 같고
        // 자리만 다르므로, 프렐류드가 `JBro::Fixed` 까지 열면 스크립트 작성자의 `String` 이 모호해진다.
        // 수식 없는 `String` 이 힙 쪽으로 풀리는지를 여기서 못박는다 - 둘 다 열리면 이 줄이 먼저 깨진다.
        static_assert(std::is_same_v<String, JBro::String>,
            "the prelude must leave String meaning the heap string");
        static_assert(false == std::is_same_v<String, JBro::Fixed::String<32>>,
            "and never the fixed one");
        // 고정 쪽은 수식해서 부르면 그대로 쓸 수 있다 - 막는 것이 아니라 이름이 겹치지 않게 하는 것이다.
        static_assert(std::is_trivially_copyable_v<JBro::Fixed::String<16>>,
            "the fixed string is still reachable by its full name");

#if defined(JBRO_TEST_FIXED_STRING_IN_PRELUDE)
        // **음성 시험.** 이 매크로를 켜면 두 이름이 한자리에 놓여 컴파일이 실패해야 한다.
        // 규칙이 말뿐이 아니라 실제로 컴파일을 막는지 손으로 켜서 확인한다.
        using namespace JBro::Fixed;
        String ambiguous;
        static_cast<void>(ambiguous);
#endif

        GameObjectHandle handle;
        Ref<ComponentBase> componentReference;
        ServiceContext services;
        Check(false == handle.IsValid() && false == static_cast<bool>(componentReference)
            && services.AbiVersion == ServiceContextAbiVersion,
            "script prelude must expose safe runtime handles and the service context");
    }
}

int RunScriptApiPreludeTests()
{
    TestScriptPreludeSurface();
    std::cout << "Script API prelude tests passed.\n";
    return 0;
}
