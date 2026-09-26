#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Framework2DSystem/System/Physics2DSystem.h>
#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/Service/Physics2DService.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/ServiceContext.h>

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
        static_assert(std::is_standard_layout_v<JBro::Framework2DSystemContext>);
        static_assert(std::is_trivially_copyable_v<JBro::Framework2DSystemContext>);
        static_assert(offsetof(JBro::Framework2DSystemContext, AbiVersion) == 0);
        static_assert(std::is_standard_layout_v<JBro::Framework3DServiceContext>);
        static_assert(std::is_trivially_copyable_v<JBro::Framework3DServiceContext>);
        static_assert(offsetof(JBro::Framework3DServiceContext, AbiVersion) == 0);
        static_assert(std::is_same_v<decltype(JBro::Framework3DServiceContext::Text3D), JBro::Service::Text3DService>);
        // 텍스트 서비스는 차원마다 따로지만 몸통은 하나다(D-224): 둘 다 공용 틀에서 나오고 가상 함수가 없다.
        static_assert(std::is_base_of_v<JBro::Service::TextServiceBase<JBro::Component::Text2D, JBro::Service::Text2DService>,
            JBro::Service::Text2DService>);
        static_assert(std::is_base_of_v<JBro::Service::TextServiceBase<JBro::Component::Text3D, JBro::Service::Text3DService>,
            JBro::Service::Text3DService>);
        static_assert(false == std::is_polymorphic_v<JBro::Service::Text3DService>);
        static_assert(std::is_same_v<decltype(JBro::Framework2DSystemContext::Text2D), JBro::System::ITextSystem*>);
        static_assert(std::is_same_v<decltype(JBro::Framework3DSystemContext::Text3D), JBro::System::ITextSystem*>);
        // 공통 SystemContext 는 차원별 시스템 슬롯을 갖지 않는다(D-43). 차원과 무관한 시간·난수·디버그 선만 든다(D-233, D-234).
        static_assert(std::is_same_v<decltype(JBro::SystemContext::Time), JBro::System::ITimeSystem*>);
        static_assert(std::is_same_v<decltype(JBro::SystemContext::Random), JBro::System::IRandomSystem*>);
        static_assert(std::is_same_v<decltype(JBro::SystemContext::DebugDraw), JBro::System::IDebugDrawSystem*>);
        static_assert(sizeof(JBro::SystemContext) == sizeof(void*) * 4);
        static_assert(false == std::is_polymorphic_v<JBro::Service::DebugDraw2DService>);
        static_assert(false == std::is_polymorphic_v<JBro::Service::DebugDraw3DService>);
        static_assert(std::is_same_v<decltype(JBro::ServiceContext::Time), JBro::Service::TimeService>);
        static_assert(std::is_same_v<decltype(JBro::ServiceContext::Random), JBro::Service::RandomService>);
        static_assert(false == std::is_polymorphic_v<JBro::Service::TimeService>);
        static_assert(false == std::is_polymorphic_v<JBro::Service::RandomService>);

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
        JBro::BindSystemContext(systems);
        Check(JBro::GetSystemContext().AbiVersion == systems.AbiVersion,
            "system context binding must copy the supplied ABI stamp");

        JBro::System::Physics2DSystem physicsMarker;
        JBro::Framework2DSystemContext systems2D;
        systems2D.Physics2D = &physicsMarker;
        JBro::BindFramework2DSystemContext(systems2D);
        Check(JBro::GetFramework2DSystems().Physics2D == &physicsMarker,
            "the 2D system context must carry the narrow physics interface pointer");
        JBro::BindFramework2DSystemContext({});

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
            JBro::RaycastHit2D& hit, std::uint32_t layerMask) const override
        {
            ++raycastCalls;
            Check(origin.x == 1.0f && origin.y == 2.0f
                && direction.x == 3.0f && direction.y == 4.0f && distance == 5.0f,
                "physics service must forward ray arguments unchanged");
            lastMask = layerMask;
            hit = {};
            if (false == hasHit)
            {
                return false;
            }
            hit.point = {6.0f, 7.0f};
            return true;
        }

        void OverlapBox(const JBro::Rect& area,
            JBro::Array<JBro::GameObjectHandle>& results, std::uint32_t layerMask) const override
        {
            ++overlapCalls;
            Check(area.min.x == 1.0f && area.min.y == 2.0f
                && area.max.x == 3.0f && area.max.y == 4.0f,
                "physics service must forward overlap bounds unchanged");
            lastMask = layerMask;
            results.Clear();
            results.Add({});
        }

        // 늘어난 질의는 서비스가 인자를 그대로 넘기는지만 센다.
        void RaycastAll(JBro::Vec2, JBro::Vec2, float distance, JBro::Array<JBro::RaycastHit2D>& hits,
            std::uint32_t layerMask) const override
        {
            ++otherCalls;
            lastMask = layerMask;
            lastDistance = distance;
            hits.Clear();
        }
        JBro::GameObjectHandle OverlapPoint(JBro::Vec2, std::uint32_t layerMask) const override
        {
            ++otherCalls;
            lastMask = layerMask;
            return {};
        }
        void OverlapCircle(JBro::Vec2, float radius, JBro::Array<JBro::GameObjectHandle>& results,
            std::uint32_t layerMask) const override
        {
            ++otherCalls;
            lastMask = layerMask;
            lastDistance = radius;
            results.Clear();
        }
        bool CircleCast(JBro::Vec2, float radius, JBro::Vec2, float, JBro::RaycastHit2D& hit,
            std::uint32_t layerMask) const override
        {
            ++otherCalls;
            lastMask = layerMask;
            lastDistance = radius;
            hit = {};
            hit.distance = 1.5f;
            return true;
        }
        bool BoxCast(JBro::Vec2, JBro::Vec2 halfExtents, float angle, JBro::Vec2, float,
            JBro::RaycastHit2D& hit, std::uint32_t layerMask) const override
        {
            ++otherCalls;
            lastMask = layerMask;
            lastDistance = halfExtents.x + angle;
            hit = {};
            hit.distance = 2.5f;
            return true;
        }

        mutable int otherCalls = 0;
        mutable std::uint32_t lastMask = 0;
        mutable float lastDistance = 0.0f;
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
                JBro::BindFramework2DSystemContext({});
            }
        } resetBinding;

        const JBro::Service::Physics2DService service;
        JBro::RaycastHit2D hit;
        hit.point = {9.0f, 9.0f};
        JBro::Array<JBro::GameObjectHandle> results;
        results.Reserve(4);
        const auto* storage = results.Data();
        const auto capacity = results.Capacity();
        results.Add({});
        JBro::BindFramework2DSystemContext({});
        Check(false == service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && hit.point.x == 0.0f && hit.point.y == 0.0f,
            "unbound physics service must fail safely and clear the previous hit");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(results.Size() == 0, "unbound physics service must clear previous overlaps");

        JBro::Framework2DSystemContext systems;
        systems.Physics2D = &first;
        JBro::BindFramework2DSystemContext(systems);
        Check(service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && first.raycastCalls == 1 && hit.point.x == 6.0f && hit.point.y == 7.0f,
            "physics service must return the bound query result with one dispatch");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(first.overlapCalls == 1 && results.Size() == 1,
            "physics service must use the caller's result storage with one dispatch");
        Check(first.lastMask == JBro::AllPhysicsLayers, "a query without a mask asks every layer");

        // 늘어난 질의도 한 번씩 그대로 넘어가고, 준 마스크가 닿는다.
        JBro::Array<JBro::RaycastHit2D> all;
        service.RaycastAll({0, 0}, {1, 0}, 7.0f, all, 0x4u);
        Check(first.otherCalls == 1 && first.lastMask == 0x4u && first.lastDistance == 7.0f,
            "RaycastAll forwards its distance and mask");
        service.OverlapPoint({0, 0}, 0x8u);
        Check(first.otherCalls == 2 && first.lastMask == 0x8u, "OverlapPoint forwards its mask");
        service.OverlapCircle({0, 0}, 3.0f, results);
        Check(first.otherCalls == 3 && first.lastDistance == 3.0f && first.lastMask == JBro::AllPhysicsLayers,
            "OverlapCircle forwards its radius");
        Check(service.CircleCast({0, 0}, 0.25f, {1, 0}, 9.0f, hit) && hit.distance == 1.5f
            && first.otherCalls == 4 && first.lastDistance == 0.25f, "CircleCast returns the bound result");
        Check(service.BoxCast({0, 0}, {1.0f, 1.0f}, 0.5f, {1, 0}, 9.0f, hit, 0x2u) && hit.distance == 2.5f
            && first.otherCalls == 5 && first.lastDistance == 1.5f && first.lastMask == 0x2u,
            "BoxCast forwards its box and mask");

        systems.Physics2D = &second;
        JBro::BindFramework2DSystemContext(systems);
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

        JBro::BindFramework2DSystemContext({});
        Check(false == service.Raycast({1.0f, 2.0f}, {3.0f, 4.0f}, 5.0f, hit)
            && hit.point.x == 0.0f && second.raycastCalls == 2,
            "unbinding must prevent calls to the old system and clear the hit");
        service.OverlapBox({{1.0f, 2.0f}, {3.0f, 4.0f}}, results);
        Check(results.Size() == 0 && second.overlapCalls == 1,
            "unbinding must clear overlaps without calling the old system");
        Check(results.Data() == storage && results.Capacity() == capacity,
            "service queries must preserve the caller's reserved storage");

        // 묶인 시스템이 없으면 늘어난 질의도 조용히 비운다.
        Check(false == service.CircleCast({0, 0}, 1.0f, {1, 0}, 9.0f, hit) && hit.distance == 0.0f,
            "an unbound CircleCast misses and clears the hit");
        Check(false == service.BoxCast({0, 0}, {1, 1}, 0.0f, {1, 0}, 9.0f, hit), "and so does BoxCast");
        Check(service.OverlapPoint({0, 0}).GetInstanceId() == JBro::InvalidInstanceId, "OverlapPoint finds nothing");
        all.Add({});
        service.RaycastAll({0, 0}, {1, 0}, 1.0f, all);
        Check(all.IsEmpty() && second.otherCalls == 0, "RaycastAll clears without calling the old system");
    }
}

int RunContextBoundaryTests()
{
    TestContextLayoutsAndBinding();
    TestPhysicsServiceBinding();
    std::cout << "Context boundary tests passed.\n";
    return 0;
}
