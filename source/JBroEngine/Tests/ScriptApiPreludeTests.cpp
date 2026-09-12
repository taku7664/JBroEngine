#include <JBro/ScriptAPI.h>

#if defined(JBRO_TEST_SCRIPT_API_SYSTEM_CONTEXT_LEAK)
#include <JBro/Runtime/SystemContext.h>
#endif

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
