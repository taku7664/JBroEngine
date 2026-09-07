#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Runtime/GameScriptBase.h>

namespace JBro
{
    // 공통 생명주기는 Runtime에 두고, 2D 충돌 훅만 이 계층에서 제공한다.
    class GameScript2D : public GameScriptBase
    {
    public:
        ~GameScript2D() override = default;

        virtual void OnCollisionEnter(const Collision2D& hit);
        virtual void OnCollisionExit(const Collision2D& hit);
    };
}
