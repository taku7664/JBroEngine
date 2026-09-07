#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Script/Macros.h>

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
}

int RunGameScriptTests()
{
    JBro::Canvas canvas(JBro::CreateDefaultAllocator());
    auto* object = canvas.CreateObject("script owner");
    auto* script = canvas.AttachComponent<ScriptProbe>(object);
    if (script == nullptr || script->GetGameObject() != object || false == script->IsActiveComponent())
    {
        throw std::runtime_error("split script base must preserve component ownership and activation");
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
    if (false == canvas.DestroyObject(object) || lifetime.IsValid())
    {
        throw std::runtime_error("split script component must retain safe pointer invalidation");
    }
    std::cout << "Game script base tests passed.\n";
    return 0;
}
