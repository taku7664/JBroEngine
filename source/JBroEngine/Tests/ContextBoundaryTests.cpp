#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>
#include <JBro/Framework2D/Service/Physics2DService.h>
#include <JBro/Framework2D/ServiceContext.h>

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
        static_assert(std::is_standard_layout_v<JBro::Framework2DServiceContext>);
        static_assert(std::is_trivially_copyable_v<JBro::Framework2DServiceContext>);
        static_assert(offsetof(JBro::Framework2DServiceContext, AbiVersion) == 0);
        static_assert(std::is_same_v<decltype(JBro::Framework2DServiceContext::Physics2D),
            JBro::Service::Physics2DService>);

        JBro::Framework2DServiceContext services2D;
        services2D.AbiVersion = JBro::Framework2DServiceContextAbiVersion + 1;
        JBro::BindFramework2DServiceContext(services2D);
        Check(JBro::GetFramework2DServices().AbiVersion == services2D.AbiVersion,
            "2D service context binding must copy its independent ABI stamp");
        JBro::BindFramework2DServiceContext({});

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

        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
    }

    class PhysicsQueryProbe final : public JBro::System::IPhysics2DSystem
    {
    public:
        mutable int raycastCalls = 0;
        mutable int overlapCalls = 0;
        bool hasHit = true;

        bool Raycast(JBro::Vec2 origin, JBro::Vec2 direction, float distance,
            JBro::Collision2D& hit) const override
        {
            ++raycastCalls;
            Check(origin.x == 1.0f && origin.y == 2.0f
                && direction.x == 3.0f && direction.y == 4.0f && distance == 5.0f,
                "physics service must forward ray arguments unchanged");
            hit = {};
            if (false == hasHit)
            {
                return false;
            }
            hit.point = {6.0f, 7.0f};
            return true;
        }

        void OverlapBox(const JBro::Rect& area,
            JBro::Array<JBro::GameObjectHandle>& results) const override
        {
            ++overlapCalls;
            Check(area.min.x == 1.0f && area.min.y == 2.0f
                && area.max.x == 3.0f && area.max.y == 4.0f,
                "physics service must forward overlap bounds unchanged");
            results.Clear();
            results.Add({});
        }
    };

    void TestPhysicsServiceBinding()
    {
        static_assert(std::is_empty_v<JBro::Service::Physics2DService>);
        static_assert(std::is_standard_layout_v<JBro::Service::Physics2DService>);
        static_assert(std::is_trivially_copyable_v<JBro::Service::Physics2DService>);

        PhysicsQueryProbe first;
        PhysicsQueryProbe second;
        struct ResetBinding
        {
            ~ResetBinding()
            {
                JBro::BindSystemContext({});
            }
        } resetBinding;

        const JBro::Service::Physics2DService service;
        JBro::Collision2D hit;
        hit.point = {9.0f, 9.0f};
        JBro::Array<JBro::GameObjectHandle> results;
        results.Reserve(4);
        const auto* storage = results.Data();
        const auto capacity = results.Capacity();
        results.Add({});
        JBro::BindSystemContext({});
        Check(false == service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && hit.point.x == 0.0f && hit.point.y == 0.0f,
            "unbound physics service must fail safely and clear the previous hit");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(results.Size() == 0, "unbound physics service must clear previous overlaps");

        JBro::SystemContext systems;
        systems.Physics2D = &first;
        JBro::BindSystemContext(systems);
        Check(service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && first.raycastCalls == 1 && hit.point.x == 6.0f && hit.point.y == 7.0f,
            "physics service must return the bound query result with one dispatch");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(first.overlapCalls == 1 && results.Size() == 1,
            "physics service must use the caller's result storage with one dispatch");

        systems.Physics2D = &second;
        JBro::BindSystemContext(systems);
        Check(service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && first.raycastCalls == 1 && second.raycastCalls == 1,
            "an existing service must use the new system after rebinding");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(first.overlapCalls == 1 && second.overlapCalls == 1,
            "overlap queries must also follow rebinding");

        second.hasHit = false;
        Check(false == service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && second.raycastCalls == 2 && hit.point.x == 0.0f,
            "physics service must preserve a bound system's miss result");

        JBro::BindSystemContext({});
        Check(false == service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && hit.point.x == 0.0f && second.raycastCalls == 2,
            "unbinding must prevent calls to the old system and clear the hit");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(results.Size() == 0 && second.overlapCalls == 1,
            "unbinding must clear overlaps without calling the old system");
        Check(results.Data() == storage && results.Capacity() == capacity,
            "service queries must preserve the caller's reserved storage");
    }
}

int RunContextBoundaryTests()
{
    TestContextLayoutsAndBinding();
    TestPhysicsServiceBinding();
    std::cout << "Context boundary tests passed.\n";
    return 0;
}
