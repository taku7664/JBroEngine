#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Script/Macros.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObjectHandle.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    template<typename T>
    concept HasCollisionEnter = requires
    {
        &T::OnCollisionEnter;
    };

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

        void OnUpdate(float deltaTime) override
        {
            elapsed += deltaTime;
        }

        float elapsed = 0.0f;
    };

    static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::GameScriptBase>);
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

int RunGameScriptTests()
{
    JBro::Canvas canvas(JBro::CreateDefaultAllocator());
    auto* object = canvas.CreateObject("script owner");
    auto* script = canvas.AttachComponent<ScriptProbe>(object);
    if (script == nullptr
        || script->GetGameObject().GetInstanceId() != object->GetInstanceId()
        || false == script->IsActiveComponent())
    {
        throw std::runtime_error("split script base must preserve component ownership and activation");
    }

    auto& registry = JBro::Internal::InstanceRegistry::Get();
    if (registry.Resolve(script->GetHandle(), JBro::RefCategory::Script) != script
        || registry.Resolve(script->GetHandle(), JBro::RefCategory::Component) != nullptr)
    {
        throw std::runtime_error("scripts must register separately from ordinary components");
    }
    auto reference = object->GetComponent<ScriptProbe>();
    auto fromHandle = object->GetScriptHandle().GetComponent<ScriptProbe>();
    auto plural = object->GetComponents<ScriptProbe>();
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
    base.OnFixedUpdate(0.25f);
    base.OnUpdate(0.5f);
    script->OnCollisionEnter({});
    script->OnCollisionExit({});
    base.OnDestroy();
    if (script->elapsed != 0.5f)
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
