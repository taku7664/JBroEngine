#include <JBro/ScriptAPI.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
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
        static_assert(std::is_same_v<decltype(Ref<GameObject>::Category), const RefCategory>);
        static_assert(std::is_class_v<PreludeScriptProbe>);

        GameObjectHandle handle;
        Ref<GameObject> reference;
        ServiceContext services;
        Check(false == handle.IsValid() && false == static_cast<bool>(reference)
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
