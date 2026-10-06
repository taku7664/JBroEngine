#include <JBro/Runtime/GameScriptBase.h>

namespace JBro
{
    Handle::GameObject GameScriptBase::GetGameObject() const
    {
        return GetOwner();
    }

    void GameScriptBase::OnCreate()
    {
    }

    void GameScriptBase::OnStart()
    {
    }

    void GameScriptBase::OnUpdate()
    {
    }

    void GameScriptBase::OnFixedUpdate()
    {
    }

    void GameScriptBase::OnDestroy()
    {
    }
}
