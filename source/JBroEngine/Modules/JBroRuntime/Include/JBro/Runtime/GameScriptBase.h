#pragma once

#include <JBro/Runtime/Component.h>

namespace JBro
{
    // Dimension-independent hooks. Collision contracts belong to each framework.
    class GameScriptBase : public ComponentBase
    {
    public:
        ~GameScriptBase() override = default;

        GameObjectHandle GetGameObject() const;

        virtual void OnCreate();
        virtual void OnStart();
        // 델타는 인자로 오지 않는다(ProjectRule §7, D-242). `GetServiceContext().Time.DeltaTime()` 으로 읽는다 -
        // `OnFixedUpdate` 안에서 읽으면 고정 델타다.
        virtual void OnUpdate();
        virtual void OnFixedUpdate();
        virtual void OnDestroy();
    };

    template<typename T>
        requires std::is_base_of_v<GameScriptBase, T>
    struct RefCategoryOf<T>
    {
        static constexpr RefCategory value = RefCategory::Script;
    };
}
