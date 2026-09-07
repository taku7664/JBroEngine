#include <JBro/Runtime/GameScriptBase.h>

namespace JBro
{
    GameObject* GameScriptBase::GetGameObject() const
    {
        return GetOwner();
    }

    void GameScriptBase::OnCreate()
    {
    }

    void GameScriptBase::OnStart()
    {
    }

    void GameScriptBase::OnUpdate(float)
    {
    }

    void GameScriptBase::OnFixedUpdate(float)
    {
    }

    void GameScriptBase::OnDestroy()
    {
    }
}
