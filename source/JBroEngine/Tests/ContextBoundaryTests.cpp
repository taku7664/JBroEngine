#include <JBro/Runtime/EngineContext.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestContextLayoutsAndBinding()
    {
        static_assert(std::is_standard_layout_v<JBro::SystemContext>);
        static_assert(std::is_trivially_copyable_v<JBro::SystemContext>);
        static_assert(offsetof(JBro::SystemContext, AbiVersion) == 0);
        static_assert(std::is_standard_layout_v<JBro::ServiceContext>);
        static_assert(std::is_trivially_copyable_v<JBro::ServiceContext>);
        static_assert(offsetof(JBro::ServiceContext, AbiVersion) == 0);

        Check(JBro::GetSystemContext().AbiVersion == JBro::SystemContextAbiVersion,
            "system context must begin with the current ABI version");
        Check(JBro::GetServiceContext().AbiVersion == JBro::ServiceContextAbiVersion,
            "service context must begin with the current ABI version");

        JBro::SystemContext systems;
        systems.AbiVersion = JBro::SystemContextAbiVersion + 1;
        JBro::System::Physics2DSystem physicsMarker;
        systems.Physics2D = &physicsMarker;
        JBro::BindSystemContext(systems);
        Check(JBro::GetSystemContext().AbiVersion == systems.AbiVersion,
            "system context binding must copy the supplied ABI stamp");
        Check(JBro::GetSystemContext().Physics2D == &physicsMarker,
            "system context binding must preserve the narrow physics interface pointer");

        JBro::ServiceContext services;
        services.AbiVersion = JBro::ServiceContextAbiVersion + 1;
        JBro::BindServiceContext(services);
        Check(JBro::GetServiceContext().AbiVersion == services.AbiVersion,
            "service context binding must copy the supplied ABI stamp");

        JBro::EngineContext engine;
        Check(engine.Platform == nullptr && engine.RHI == nullptr
            && engine.Renderer == nullptr && engine.Assets == nullptr,
            "engine context must default every borrowed process pointer to null");
        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
    }
}

int RunContextBoundaryTests()
{
    TestContextLayoutsAndBinding();
    std::cout << "Context boundary tests passed.\n";
    return 0;
}
