#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Handle/Collider2D.h>
#include <JBro/Framework2D/Handle/Rigidbody2D.h>
#include <JBro/Framework2D/Handle/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Script/Macros.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObjectHandle.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <JBro/Types/Int.h>

namespace
{
    template<typename T>
    concept HasCollisionEnter = requires
    {
        &T::OnCollisionEnter;
    };

    template<typename T>
    concept HasTriggerHooks = requires
    {
        &T::OnTriggerEnter;
        &T::OnTriggerStay;
        &T::OnTriggerExit;
        &T::OnCollisionStay;
    };

    static_assert(HasTriggerHooks<JBro::GameScript2D>, "2D scripts receive trigger hooks (D-207)");
    static_assert(false == HasTriggerHooks<JBro::GameScriptBase>, "and the dimension-free base does not");

    JBRO_SCRIPT(ScriptProbe) final : public JBro::GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::ScriptProbe";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate() override
        {
            ++updates;
        }

        void OnFixedUpdate() override
        {
            ++fixedUpdates;
        }

        JBro::Int32 updates = 0;
        JBro::Int32 fixedUpdates = 0;
    };

    // 스크립트는 컴포넌트가 아니다(D-302).
    static_assert(false == std::is_base_of_v<JBro::ComponentBase, JBro::GameScriptBase>);
    static_assert(std::is_base_of_v<JBro::GameScriptBase, JBro::GameScript2D>);
    static_assert(std::is_abstract_v<JBro::GameScriptBase>);
    static_assert(std::is_abstract_v<JBro::GameScript2D>);
    static_assert(false == std::is_abstract_v<ScriptProbe>);
    static_assert(false == HasCollisionEnter<JBro::GameScriptBase>);
    static_assert(HasCollisionEnter<JBro::GameScript2D>);
    static_assert(JBro::Ref<ScriptProbe>::Category == JBro::RefCategory::Script);
    static_assert(JBro::Ref<JBro::GameScriptBase>::Category == JBro::RefCategory::Script);
    static_assert(JBro::Ref<JBro::Component::Collider2D>::Category == JBro::RefCategory::Component);
}

namespace
{
    void Expect(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    // **빌트인 컴포넌트 핸들**(D-302). 오브젝트 핸들의 `GetComponent<핸들>` 이 그 컴포넌트를 가리키고, 핸들의 멤버가 데이터를
    // 읽고 쓰며, 월드 캐시는 읽기만 하고, 컴포넌트가 떼이면 핸들은 무효가 되어 쓰기를 무시한다.
    void TestBuiltinComponentHandles()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        auto* object = canvas.CreateObject("handle owner");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        auto* body = canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        Expect(transform != nullptr && body != nullptr, "the builtins attach");

        const JBro::Handle::GameObject owner = object->GetScriptHandle();
        JBro::Handle::Transform2D handle = owner.GetComponent<JBro::Handle::Transform2D>();
        Expect(handle.IsValid() && handle.GetGameObject().GetInstanceId() == object->GetInstanceId(),
            "GetComponent with a handle type finds the component and its owner");
        Expect(false == owner.GetComponent<JBro::Handle::Collider2D>().IsValid(),
            "a component the object does not have gives an empty handle");

        handle.SetPosition({3.0f, -2.0f});
        handle.SetRotation(90.0f);
        handle.SetScale({2.0f, 4.0f});
        Expect(transform->position.x == 3.0f && transform->position.y == -2.0f
                && transform->scale.x == 2.0f && transform->scale.y == 4.0f,
            "the handle writes the component's authored values");
        Expect(handle.GetRotation().Value > 89.99f && handle.GetRotation().Value < 90.01f
                && transform->GetRotationRadian().Value > 1.5707f && transform->GetRotationRadian().Value < 1.5709f,
            "rotation goes in and out of the handle in degrees and is stored in radians");

        transform->worldPosition = {7.0f, 8.0f};
        transform->worldValid = true;
        Expect(handle.GetWorldPosition().x == 7.0f && handle.GetWorldPosition().y == 8.0f && handle.GetWorldValid(),
            "the handle reads the world cache the system fills");

        JBro::Handle::Rigidbody2D bodyHandle = owner.GetComponent<JBro::Handle::Rigidbody2D>();
        bodyHandle.AddForce({1.0f, 2.0f});
        bodyHandle.SetGravityScale(0.5f);
        const JBro::Component::PendingForces2D pending = body->TakePendingForces();
        Expect(pending.forceAtCenter.x == 1.0f && pending.forceAtCenter.y == 2.0f && body->gravityScale == 0.5f,
            "a body handle forwards forces and writes fields");

        bodyHandle.SetEnabled(false);
        Expect(false == body->IsEnabled() && false == bodyHandle.IsEnabled(), "the handle turns its component off");

        const JBro::Handle::Transform2D copy = handle;
        Expect(copy == handle, "copies of a handle point at the same component");
        Expect(canvas.DestroyObject(object), "the object goes away");
        Expect(false == handle.IsValid() && false == copy.IsValid(), "every copy goes invalid with the component");
        handle.SetPosition({9.0f, 9.0f});
        Expect(handle.GetPosition().x == 0.0f && handle.GetWorldValid() == false,
            "an invalid handle ignores writes and reads defaults");
    }
}

JBro::Int32 RunGameScriptTests()
{
    TestBuiltinComponentHandles();

    JBro::Canvas canvas(JBro::CreateDefaultAllocator());
    auto* object = canvas.CreateObject("script owner");
    auto* script = canvas.AttachScript<ScriptProbe>(object);
    if (script == nullptr
        || script->GetGameObject().GetInstanceId() != object->GetInstanceId()
        || false == script->IsActiveScript())
    {
        throw std::runtime_error("split script base must preserve component ownership and activation");
    }

    auto& registry = JBro::Internal::InstanceRegistry::Get();
    if (registry.Resolve(script->GetHandle(), JBro::RefCategory::Script) != script
        || registry.Resolve(script->GetHandle(), JBro::RefCategory::Component) != nullptr)
    {
        throw std::runtime_error("scripts must register separately from ordinary components");
    }
    auto reference = object->GetScript<ScriptProbe>();
    auto fromHandle = object->GetScriptHandle().GetScript<ScriptProbe>();
    auto plural = object->GetScripts<ScriptProbe>();
    if (reference.Get() != script || fromHandle.Get() != script
        || plural.Size() != 1 || plural[0].Get() != script)
    {
        throw std::runtime_error("all component query surfaces must resolve Script-category references");
    }
    reference.Cached = {};
    if (reference.Get() != script)
    {
        throw std::runtime_error("script reference must recover its cache by persistent identity");
    }
    const auto lookups = registry.GetPersistentLookupCount();
    if (reference.Get() != script || registry.GetPersistentLookupCount() != lookups)
    {
        throw std::runtime_error("cached script reference must bypass persistent lookup");
    }

    // Direct calls verify the inherited API and linking, not ScriptSystem scheduling.
    JBro::GameScriptBase& base = *script;
    base.OnCreate();
    base.OnStart();
    base.OnFixedUpdate();
    base.OnUpdate();
    script->OnCollisionEnter({});
    script->OnCollisionExit({});
    base.OnDestroy();
    if (script->updates != 1 || script->fixedUpdates != 1)
    {
        throw std::runtime_error("common script hook must dispatch through the Runtime base");
    }

    auto lifetime = script->SafeFromThis();
    if (false == canvas.DestroyObject(object) || lifetime.IsValid() || reference.Get() != nullptr)
    {
        throw std::runtime_error("split script component must retain safe pointer invalidation");
    }
    std::cout << "Game script base tests passed.\n";
    return 0;
}
